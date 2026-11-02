/*
 * BoardConfig.h — Milk-V Duo S（SG2000 / CV181x，PR-B3）
 *
 * 仅 HAL / Board 包含；禁止 Common / Services 直接 include。
 * 厂商预装 U-Boot 为 RISC-V（C906）；本包落在 HAL/RiscV/Board/。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "milk-v-duo-s"
#define BOARD_ARCH             "RiscV"

/* SG2000 UART0：DW APB 16550 兼容，寄存器间距 ×4（reg-shift=2） */
#define BOARD_UART_BASE        0x04140000ULL
#define BOARD_UART_REG_SHIFT   2
#define BOARD_UART_BAUD        115200

/* 与厂商 U-Boot 环境变量对齐（kernel_addr_r / fdt_addr_r） */
#define BOARD_KERNEL_LOAD      0x80200000ULL
#define BOARD_DTB_LOAD         0x81200000ULL

/* DTB 缺失时的 DRAM fallback（U-Boot 常见 ~510MiB @0x80000000） */
#define BOARD_RAM_BASE         0x80000000ULL
#define BOARD_RAM_SIZE         (510ULL * 1024ULL * 1024ULL)

/* cv18xx.dtsi cpus.timebase-frequency；SD 延时用，勿改 virt 的 10MHz 定时器 */
#define BOARD_TIMEBASE_HZ      25000000u

/* 命令行靶：无 FB。块设备 = µSD（Sdhci.c PIO），不是 virtio */
#define BOARD_HAS_FRAMEBUFFER  0
#define BOARD_HAS_BLOCK        1
#define BOARD_HAS_NET          1
/* audio-duo-1：3.5mm = 片内 DAC ← I2S3 TX（Docs/29 §6.2） */
#define BOARD_HAS_AUDIO        1
/* dwifi-netif-1：AIC8800D80（SDIO fn1）。定义了它，LwIp/Netif.c 的 WiFi 出口钩子才不会被剃；
 * 未武装时 Try 返回 -1，仍回退 dwmac —— 与 Pi 的 BOARD_HAS_WIFI 同语义。 */
#define BOARD_HAS_WIFI         1
/* bt-duo-1：AIC8800 HCI 走 UART4（Docs/27）；与壳 UART0 分口 */
#define BOARD_HAS_BLUETOOTH    1
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0

#endif /* BOARD_CONFIG_H */
