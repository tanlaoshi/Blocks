/*
 * BoardConfig.h — QEMU riscv64「virt」板的地址约定
 *
 * 【初学者】与 Arm64 的 BoardConfig 同角色：UART/RAM/加载址等板相关宏。
 * 仅 Hal / Board include；Common 走门面或 BOOT_INFO。
 */
#ifndef TOY_BOARD_CONFIG_H
#define TOY_BOARD_CONFIG_H

#define TOY_BOARD_NAME             "virt"
#define TOY_BOARD_ARCH             "RiscV"

/* QEMU virt UART16550（字节间距） */
#define TOY_BOARD_UART_BASE        0x10000000ULL
#define TOY_BOARD_UART_REG_SHIFT   0
#define TOY_BOARD_UART_BAUD        115200

/* 与 link.ld / OpenSBI payload 一致（内核 @0x80200000；DRAM @0x80000000） */
#define TOY_BOARD_KERNEL_LOAD      0x80200000ULL
#define TOY_BOARD_DTB_LOAD         0x00000000ULL /* 由 OpenSBI/QEMU 交接 */
#define TOY_BOARD_RAM_BASE         0x80000000ULL
#define TOY_BOARD_RAM_SIZE         (256ULL * 1024ULL * 1024ULL)

/* virt 默认可有内存帧缓冲；串口子集由运行时 HalHasFrameBuffer 决定 */
#define TOY_BOARD_HAS_FRAMEBUFFER  1
#define TOY_BOARD_HAS_BLOCK        1 /* virtio-blk */
#define TOY_BOARD_HAS_NET          1 /* virtio-net */
#define TOY_BOARD_CONSOLE_ONLY     0
#define TOY_BOARD_IS_VIRT          1

#endif /* TOY_BOARD_CONFIG_H */
