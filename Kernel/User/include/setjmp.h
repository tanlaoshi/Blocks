/*
 * setjmp.h — 非局部跳转（libc-setjmp）
 * x86_64：rbx rbp r12–r15 rsp rip（8）
 * aarch64：x19–x28 fp lr sp（14，槽 16）
 * riscv：s0–s11 ra sp（14，槽 16）
 */
#ifndef SETJMP_H
#define SETJMP_H

#if defined(__aarch64__) || defined(__riscv)
typedef unsigned long jmp_buf[16];
#else
typedef unsigned long jmp_buf[8];
#endif

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
