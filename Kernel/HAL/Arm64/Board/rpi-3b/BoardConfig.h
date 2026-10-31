/*
 * BoardConfig.h — Raspberry Pi 3B（BCM2837）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * 地址以公开 BCM2837 / 树莓派外设文档为准；真机以 DTB / U-Boot printenv 复核。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "rpi-3b"
#define BOARD_ARCH             "Arm64"

/*
 * BCM2837 ARM 物理外设窗 @ 0x3F000000。
 *
 * 手测（2026-10-24）：本卡 U-Boot 2021.01 无 disable-bt 时串口出字；
 * 加 dtoverlay=disable-bt 则整卡无 U-Boot 提示 → GPIO14/15 上是 **mini-UART**。
 * Blocks 须跟同一口，勿再打 PL011（0x3F201000），也勿猜改波特率（固件/U-Boot 已配 115200 + core_freq=250）。
 *
 * AUX 块 @0x3F215000（BCM2835 ARM Peripherals §2.2）；IO/LSR 见 HalSerial。
 */
#define BOARD_UART_MINI        1
#define BOARD_UART_BASE        0x3F215000ULL
#define BOARD_UART_REG_SHIFT   0
#define BOARD_UART_BAUD        115200

/* 与常见 U-Boot 环境对齐的提示值；以板上 printenv 为准 */
#define BOARD_KERNEL_LOAD      0x00080000ULL
#define BOARD_DTB_LOAD         0x02400000ULL

/* DTB 缺失时的 DRAM fallback（3B 常见 1GiB @0）；真机优先 DTB /memory（rpi-4） */
#define BOARD_RAM_BASE         0x00000000ULL
#define BOARD_RAM_SIZE         (1024ULL * 1024ULL * 1024ULL)

/* 本柱命令行靶：无 FB → 串口壳；块/网后置 */
#define BOARD_HAS_FRAMEBUFFER  0
#define BOARD_HAS_BLOCK        1 /* rpi-12：SDHCI @0x3F300000 → BLOCKS */
#define BOARD_HAS_NET          1 /* rnet-2：LAN9514 USB smsc95xx */
#define BOARD_HAS_WIFI         1 /* pi-wifi-7：BCM43430 SDIO；linkoutput 可走 WiFi */
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0
/*
 * VC scanout 默认 RGB（低字节=R）；UI 为 0x00RRGGBB（低字节=B）。
 * mailbox SET_PIXORD 常被忽略 → HalVideoSet 开软件 R/B 对调。
 */
#define BOARD_FB_SWAP_RB       1

#endif /* BOARD_CONFIG_H */
