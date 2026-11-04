/*
 * stdarg.h — freestanding <stdarg.h> 实现（编译器垫片）
 *
 * ISO C99 规定本头的用法，不规定 va_list 如何落地。
 * 此处用 gcc/clang builtin；业务只写 va_start/va_arg/…，勿直接 __builtin_*。
 * 见 Include/Abi/Compiler.h（c99-1 方言口径）。
 */
#ifndef STDARG_H
#define STDARG_H

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

#endif
