/*
 * Compiler.h — 编译器垫片（c99-1 / c99-2 / c99-3）
 *
 * Kernel 业务锚定 ISO C99；硬件布局 / 调用约定 / 编译期断言等
 * 标准未规定或 C11 才有的写法，只在本头落地。换编译器时优先改这里。
 *
 * 允许：BLOCKS_PACKED / ALIGNED / SECTION / USED / NORETURN / MS_ABI /
 *       STATIC_ASSERT / OFFSETOF
 * 禁止：Kernel 业务手写 __attribute__ / __builtin_* / _Static_assert
 *      （stdarg.h 除外；Boot/ThirdParty/lwIP 源不收）
 */
#ifndef ABI_COMPILER_H
#define ABI_COMPILER_H

#if defined(__GNUC__) || defined(__clang__)

#define BLOCKS_PACKED           __attribute__((packed))
#define BLOCKS_ALIGNED(N)       __attribute__((aligned(N)))
#define BLOCKS_SECTION(Name)    __attribute__((section(Name)))
#define BLOCKS_USED             __attribute__((used))
#define BLOCKS_NORETERN         __attribute__((noreturn))
#define BLOCKS_MS_ABI           __attribute__((ms_abi))

#else
#error "Compiler.h: define BLOCKS_PACKED / BLOCKS_ALIGNED for this compiler"
#endif

/* C99 编译期断言（替代 C11 _Static_assert）；msg 仅作文档，不进类型名 */
#define BLOCKS_STATIC_ASSERT_PASTE2(A, B) A##B
#define BLOCKS_STATIC_ASSERT_PASTE(A, B)  BLOCKS_STATIC_ASSERT_PASTE2(A, B)
#define BLOCKS_STATIC_ASSERT(Cond, Msg) \
    typedef char BLOCKS_STATIC_ASSERT_PASTE(BlocksStaticAssert_, __LINE__) \
        [((Cond) ? 1 : -1)]

/*
 * 成员偏移。业务写 BLOCKS_OFFSETOF，不写 __builtin_offsetof。
 * GNUC/Clang 用 builtin，保证可进编译期断言。
 */
#if defined(__GNUC__) || defined(__clang__)
#define BLOCKS_OFFSETOF(Type, Member) __builtin_offsetof(Type, Member)
#else
#define BLOCKS_OFFSETOF(Type, Member) \
    ((unsigned long)&(((Type *)0)->Member))
#endif

#endif /* ABI_COMPILER_H */
