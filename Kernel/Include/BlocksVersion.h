/*
 * BlocksVersion.h — 产品内核语义化版本（三架构共用）
 *
 * 展示形如「Blocks V 0.1.0」。改版本只动本头；勿在各板复制字面量。
 */
#ifndef BLOCKS_VERSION_H
#define BLOCKS_VERSION_H

#define BLOCKS_VERSION_MAJOR 0
#define BLOCKS_VERSION_MINOR 1
#define BLOCKS_VERSION_PATCH 0

#define BLOCKS_VERSION_STRING "0.1.0"

static inline const char *BlocksVersionArchitectureName(void) {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__riscv)
    return "riscv64";
#else
    return "unknown";
#endif
}

/* who：课堂口径标签（X64 / ARM64 / RISCV） */
static inline const char *BlocksWhoArchitectureName(void) {
#if defined(__x86_64__)
    return "X64";
#elif defined(__aarch64__)
    return "ARM64";
#elif defined(__riscv)
    return "RISCV";
#else
    return "UNKNOWN";
#endif
}

#endif /* BLOCKS_VERSION_H */
