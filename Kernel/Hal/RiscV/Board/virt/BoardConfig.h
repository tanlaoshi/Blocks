/*
 * BoardConfig.h — QEMU riscv64「virt」板的地址约定
 *
 * 【初学者】与 Arm64 的 BoardConfig 同角色：UART/RAM/加载址等板相关宏。
 * 仅 Hal / Board include；Common 走门面或 BOOT_INFO。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "virt"
#define BOARD_ARCH             "RiscV"

/* QEMU virt UART16550（字节间距） */
#define BOARD_UART_BASE        0x10000000ULL
#define BOARD_UART_REG_SHIFT   0
#define BOARD_UART_BAUD        115200

/* 与 link.ld / OpenSBI payload 一致（内核 @0x80200000；DRAM @0x80000000） */
#define BOARD_KERNEL_LOAD      0x80200000ULL
#define BOARD_DTB_LOAD         0x00000000ULL /* 由 OpenSBI/QEMU 交接 */
#define BOARD_RAM_BASE         0x80000000ULL
#define BOARD_RAM_SIZE         (256ULL * 1024ULL * 1024ULL)

/* virt 默认可有内存帧缓冲；串口子集由运行时 HalHasFrameBuffer 决定 */
#define BOARD_HAS_FRAMEBUFFER  1
#define BOARD_HAS_BLOCK        1 /* virtio-blk */
#define BOARD_HAS_NET          1 /* virtio-net */
#define BOARD_CONSOLE_ONLY     0
#define BOARD_IS_VIRT          1

#endif /* BOARD_CONFIG_H */
