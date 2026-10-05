/* Shadows newlib's setjmp.h (this folder comes first in -I): a 512-byte jmp_buf
for port/ps3/src/ps3_setjmp.S, which replaces the broken setjmp of libc.a. */

#ifndef __HALO_PS3_SETJMP_H
#define __HALO_PS3_SETJMP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef double jmp_buf[64];

extern void longjmp(jmp_buf, int);
extern int setjmp(jmp_buf);

#ifdef __cplusplus
}
#endif

#endif
