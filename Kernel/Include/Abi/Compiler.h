/*
 * Compiler.h — 编译器垫片（c99-1）
 *
 * 业务代码锚定 ISO C99；硬件布局 / 调用约定等标准未规定的部分
 * 只在本头对具体编译器展开。换编译器时优先改这里。
 *
 * 允许：BLOCKS_PACKED / ALIGNED / NORETURN / MS_ABI
 * 禁止：业务文件手写 __attribute__ / __builtin_*（stdarg.h 除外）
 */
#ifndef ABI_COMPILER_H
#define ABI_COMPILER_H

#if defined(__GNUC__) || defined(__clang__)

#define BLOCKS_PACKED           __attribute__((packed))
#define BLOCKS_ALIGNED(N)       __attribute__((aligned(N)))
#define BLOCKS_NORETERN         __attribute__((noreturn))
#define BLOCKS_MS_ABI           __attribute__((ms_abi))

#else
#error "Compiler.h: define BLOCKS_PACKED / BLOCKS_ALIGNED for this compiler"
#endif

#endif /* ABI_COMPILER_H */
