/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"

/* Testcase: Submit patches with relocations in memory that will fault
 *
 * To be really evil, use a gtt mmap for them.
 */

#define OBJECT_SIZE 16384

#define COPY_BLT_CMD		(2<<29|0x53<<22|0x6)
#define BLT_WRITE_ALPHA		(1<<21)
#define BLT_WRITE_RGB		(1<<20)
#define BLT_SRC_TILED		(1<<15)
#define BLT_DST_TILED		(1<<11)

static void *
mmap_bo(int fd, uint32_t handle)
{
	struct drm_i915_gem_mmap_gtt arg;
	void *ptr;
	int ret;

	memset(&arg, 0, sizeof(arg));

	arg.handle = handle;
	ret = ioctl(fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &arg);
	assert(ret == 0);

	ptr = mmap(0, 4096, PROT_READ | PROT_WRITE,
		   MAP_SHARED, fd, arg.offset);
	assert(ptr != MAP_FAILED);

	return ptr;
}

static uint32_t gem_create(int fd, int size)
{
	struct drm_i915_gem_create create;

	create.handle = 0;
	create.size = (size + 4095) & -4096;
	(void)drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE, &create);

	return create.handle;
}

static int gem_write(int fd,
		     uint32_t handle, uint32_t offset,
		     const void *src, int length)
{
	struct drm_i915_gem_pwrite pwrite;

	pwrite.handle = handle;
	pwrite.offset = offset;
	pwrite.size = length;
	pwrite.data_ptr = (uintptr_t)src;
	return drmIoctl(fd, DRM_IOCTL_I915_GEM_PWRITE, &pwrite);
}

static int gem_linear_blt(uint32_t *batch,
			  uint32_t src,
			  uint32_t dst,
			  uint32_t length,
			  struct drm_i915_gem_relocation_entry *reloc)
{
	uint32_t *b = batch;
	int height = length / (16 * 1024);

	assert(height <= 1<<16);

	if (height) {
		b[0] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		b[1] = 0xcc << 16 | 1 << 25 | 1 << 24 | (16*1024);
		b[2] = 0;
		b[3] = height << 16 | (4*1024);
		b[4] = 0;
		reloc->offset = (b-batch+4) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = dst;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;
		reloc->presumed_offset = 0;
		reloc++;

		b[5] = 0;
		b[6] = 16*1024;
		b[7] = 0;
		reloc->offset = (b-batch+7) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = src;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = 0;
		reloc->presumed_offset = 0;
		reloc++;

		b += 8;
		length -= height * 16*1024;
	}
	
	if (length) {
		b[0] = COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB;
		b[1] = 0xcc << 16 | 1 << 25 | 1 << 24 | (16*1024);
		b[2] = height << 16;
		b[3] = (1+height) << 16 | (length / 4);
		b[4] = 0;
		reloc->offset = (b-batch+4) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = dst;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = I915_GEM_DOMAIN_RENDER;
		reloc->presumed_offset = 0;
		reloc++;

		b[5] = height << 16;
		b[6] = 16*1024;
		b[7] = 0;
		reloc->offset = (b-batch+7) * sizeof(uint32_t);
		reloc->delta = 0;
		reloc->target_handle = src;
		reloc->read_domains = I915_GEM_DOMAIN_RENDER;
		reloc->write_domain = 0;
		reloc->presumed_offset = 0;
		reloc++;

		b += 8;
	}

	b[0] = MI_BATCH_BUFFER_END;
	b[1] = 0;

	return (b+2 - batch) * sizeof(uint32_t);
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close close;

	close.handle = handle;
	(void)drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static void gem_sync(int fd, uint32_t handle)
{
	struct drm_i915_gem_set_domain set_domain;

	set_domain.handle = handle;
	set_domain.read_domains = I915_GEM_DOMAIN_GTT;
	set_domain.write_domain = I915_GEM_DOMAIN_GTT;

	drmIoctl(fd, DRM_IOCTL_I915_GEM_SET_DOMAIN, &set_domain);
}

static void gem_exec(int fd, struct drm_i915_gem_execbuffer2 *execbuf)
{
	int ret;

	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       execbuf);
	assert(ret == 0);
}

static void run(int object_size)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec[3];
	struct drm_i915_gem_relocation_entry reloc[4];
	uint32_t buf[20];
	uint32_t handle, handle_relocs, src, dst;
	void *gtt_relocs;
	int fd, len;
	int ring, ret;

	fd = drm_open_any();
	handle = gem_create(fd, 4096);
	src = gem_create(fd, object_size);
	dst = gem_create(fd, object_size);

	len = gem_linear_blt(buf, src, dst, object_size, reloc);
	gem_write(fd, handle, 0, buf, len);

	exec[0].handle = src;
	exec[0].relocation_count = 0;
	exec[0].relocs_ptr = 0;
	exec[0].alignment = 0;
	exec[0].offset = 0;
	exec[0].flags = 0;
	exec[0].rsvd1 = 0;
	exec[0].rsvd2 = 0;

	exec[1].handle = dst;
	exec[1].relocation_count = 0;
	exec[1].relocs_ptr = 0;
	exec[1].alignment = 0;
	exec[1].offset = 0;
	exec[1].flags = 0;
	exec[1].rsvd1 = 0;
	exec[1].rsvd2 = 0;

	handle_relocs = gem_create(fd, 4096);
	ret = gem_write(fd, handle_relocs, 0, reloc, sizeof(reloc));
	assert(ret == 0);
	gtt_relocs = mmap_bo(fd, handle_relocs);
	assert(gtt_relocs);

	exec[2].handle = handle;
	exec[2].relocation_count = len > 40 ? 4 : 2;
	/* A newly mmap gtt bo will fault on first access. */
	exec[2].relocs_ptr = (uintptr_t)gtt_relocs;
	exec[2].alignment = 0;
	exec[2].offset = 0;
	exec[2].flags = 0;
	exec[2].rsvd1 = 0;
	exec[2].rsvd2 = 0;

	ring = 0;
	if (HAS_BLT_RING(intel_get_drm_devid(fd)))
		ring = I915_EXEC_BLT;

	execbuf.buffers_ptr = (uintptr_t)exec;
	execbuf.buffer_count = 3;
	execbuf.batch_start_offset = 0;
	execbuf.batch_len = len;
	execbuf.cliprects_ptr = 0;
	execbuf.num_cliprects = 0;
	execbuf.DR1 = 0;
	execbuf.DR4 = 0;
	execbuf.flags = ring;
	execbuf.rsvd1 = 0;
	execbuf.rsvd2 = 0;

	gem_exec(fd, &execbuf);
	gem_sync(fd, handle);

	gem_close(fd, handle);

	close(fd);
}

int main(int argc, char **argv)
{
	run(OBJECT_SIZE);

	return 0;
}
