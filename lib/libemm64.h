#include <sys/types.h>

#ifdef __i386__

/* 32 bit application */

__BEGIN_DECLS

/* mmap64 family of syscalls.  Requires kernel support. */
u_int64_t mmap64(u_int64_t addr, u_int64_t len, int prot, int flags,
	    int fd, off_t offset);
int     munmap64(u_int64_t addr, u_int64_t len);
int     msync64(u_int64_t addr, u_int64_t len, int flags);

/* generic bcopy/bzero functions. */
extern void bcopy64(u_int64_t src, u_int64_t dst, u_int64_t len);
extern void bzero64(u_int64_t dst, u_int64_t len);

/* Note: ANSI C memset returns first arg as void *.  Not this one. */
extern void memset64(u_int64_t dst, int val, u_int64_t len);

/*
 * Call your own 64 bit code functions.  It is called like this:
 *   u_int32_t (*func)(void *arg);
 * Pass an argument structure as 'arg'.  If you need more than a 32 bit
 * return, put the return value in the 'arg' struct.  Compile the code
 * fragment on a 64 bit freebsd-6 box, or any gcc that accepts -m64. You
 * can link on a 32 bit freebsd-6 box, or try 'ld -r' or file2c etc.
 * Restrictions: the 64 bit code blob must be self contained.  It may not call
 * other functions nor reference symbols or a linker error will happen.
 * Ignore the warning:
 * warning: i386:x86-64 architecture of input file `func64.o' is incompatible with i386 output
 */
extern u_int32_t tramp64(void *func, void *arg);

/*
 * Load a simple 64 bit .so file for use with tramp64().  It should be -fpic.
 * It must be self contained and make no external symbol or library references.
 * Link it with libc.a or libc_pic.a if necessary to satisfy this.  There
 * is NO support for the usual shlib magic (init,fini etc), so there are no
 * static constructors etc.  I'd recommend using simple C here anyway.
 */
extern void *dlopen64(char *path);
extern void *dlsym64(void *handle, char *func);
extern void dlclose64(void *handle);

__END_DECLS

#else

/* 64 bit application - just inlines. library is a stub. */
#include <sys/mman.h>

static __inline u_int64_t
mmap64(u_int64_t addr, u_int64_t len, int prot, int flags,
	    int fd, off_t offset)
{
	return (u_int64_t)mmap((void *)addr, len, prot, flags, fd, offset);
}
static __inline int
munmap64(u_int64_t addr, u_int64_t len)
{
	return munmap((void *)addr, len);
}
static __inline int
msync64(u_int64_t addr, u_int64_t len, int flags)
{
	return msync((void *)addr, len, flags);
}

static __inline void
bcopy64(u_int64_t src, u_int64_t dst, u_int64_t len)
{
	bcopy((void *)src, (void *)dst, len);
}
static __inline void
bzero64(u_int64_t dst, u_int64_t len)
{
	bzero((void *)dst, len);
}
static __inline void
memset64(u_int64_t dst, int val, u_int64_t len)
{
	memset((void *)dst, val, len);
}

#endif	/* end of arch-specific */

/* Generic helper functions */
static __inline void
memcpy_to64(uintptr_t dst, void *src, size_t len)
{
        bcopy64((uintptr_t)src, dst, len);
}

static __inline void
memcpy_from64(void *dst, u_int64_t src, size_t len)
{
        bcopy64(src, (uintptr_t)dst, len);
}

static __inline void
memcpy64(u_int64_t dst, u_int64_t src, u_int64_t len)
{
        bcopy64(src, dst, len);
}

static __inline void
memmove64(u_int64_t dst, u_int64_t src, u_int64_t len)
{
        bcopy64(src, dst, len);
}

