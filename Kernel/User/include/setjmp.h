/*
 * setjmp.h — 非局部跳转（libc-setjmp；x86_64 先）
 */
#ifndef SETJMP_H
#define SETJMP_H

typedef unsigned long jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
