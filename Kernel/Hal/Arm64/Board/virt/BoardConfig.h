/*
 * BoardConfig.h — QEMU aarch64「virt」板的地址约定
 *
 * 【初学者】
 * 同一套 Arm64 代码将来可能跑在不同板子上。板相关的「UART 在哪、
 * 内核加载到哪」集中写在这里，HalSerial / Handoff 来读宏。
 *
 * 仅 Hal / Board 可 include；Common 不要直接依赖板地址——
 * 应通过 Hal 门面或 BOOT_INFO。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "virt"
#define BOARD_ARCH             "Arm64"

/* QEMU virt PL011 */
#define BOARD_UART_BASE        0x09000000ULL
#define BOARD_UART_BAUD        115200

/* QEMU -kernel 链接/加载提示（见 HAL/Arm64/link.ld、Startup.c） */
#define BOARD_KERNEL_LOAD      0x40000000ULL
#define BOARD_DTB_LOAD         0x4a000000ULL

/* virt 默认可有内存帧缓冲；串口子集由运行时 HalHasFrameBuffer 决定 */
#define BOARD_HAS_FRAMEBUFFER  1
#define BOARD_HAS_BLOCK        1 /* virtio-blk */
#define BOARD_HAS_NET          1 /* virtio-net */
#define BOARD_CONSOLE_ONLY     0
#define BOARD_IS_VIRT          1

#endif /* BOARD_CONFIG_H */
