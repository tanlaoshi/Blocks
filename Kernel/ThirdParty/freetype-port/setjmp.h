#ifndef BLOCKS_FT_SETJMP_H_
#define BLOCKS_FT_SETJMP_H_

typedef unsigned long jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
