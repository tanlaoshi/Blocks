/*
 * BoardConfig.h — QEMU aarch64 virt（PR-B2）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * HalSerial 用 BOARD_UART_BASE；能力旗标仍由 HalHasFrameBuffer 等门面暴露。
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

/* virt 默认可有 ramfb；串口子集由运行时 HalHasFrameBuffer 决定 */
#define BOARD_HAS_FRAMEBUFFER  1
#define BOARD_HAS_BLOCK        1 /* virtio-blk */
#define BOARD_HAS_NET          1 /* virtio-net */
#define BOARD_CONSOLE_ONLY     0
#define BOARD_IS_VIRT          1

#endif /* BOARD_CONFIG_H */
