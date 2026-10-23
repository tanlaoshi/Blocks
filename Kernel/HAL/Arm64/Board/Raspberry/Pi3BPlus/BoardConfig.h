/*
 * BoardConfig.h — Raspberry Pi 3B+（BCM2837B0）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * 地址以公开 BCM2837 / 树莓派外设文档为准；真机以 DTB / U-Boot printenv 复核。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "Raspberry/Pi3BPlus"
#define BOARD_ARCH             "Arm64"

/*
 * BCM2837 ARM 物理外设窗 @ 0x3F000000。
 * PL011 UART0：0x3F201000（与 HalSerial PL011 驱动同形）。
 * 若发行版固件把控制台绑在 mini-UART，见 NOTES.md，勿在此猜改。
 */
#define BOARD_UART_BASE        0x3F201000ULL
#define BOARD_UART_REG_SHIFT   0
#define BOARD_UART_BAUD        115200

/* 与常见 U-Boot 环境对齐的提示值；以板上 printenv 为准 */
#define BOARD_KERNEL_LOAD      0x00080000ULL
#define BOARD_DTB_LOAD         0x02400000ULL

/* DTB 缺失时的 DRAM fallback（3B+ 常见 1GiB @0）；真机优先 DTB /memory（rpi-4） */
#define BOARD_RAM_BASE         0x00000000ULL
#define BOARD_RAM_SIZE         (1024ULL * 1024ULL * 1024ULL)

/* 本柱命令行靶：无 FB → 串口壳；块/网后置 */
#define BOARD_HAS_FRAMEBUFFER  0
#define BOARD_HAS_BLOCK        0
#define BOARD_HAS_NET          0
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0

#endif /* BOARD_CONFIG_H */
