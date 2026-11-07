/*
 * BoardConfig.h — 小米 MIX 2（chiron，MSM8998）
 *
 * 控制台：aboot 已扫的 cont_splash；不点亮 DSI、不做板上 UART。
 * 加载：gzip(Image)+DTB → boot-chiron.img；phys=0x80008000（kernel_addr=0x8000）。
 * splash：0x9D600000 / stride 4320 / 32bpp（PR-MIX2-2 真机绿）。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "mix-2"
#define BOARD_ARCH             "Arm64"

/* 无 UART；HalSerial 仍需占位基址（BRINGUP splash 路径不调用） */
#define BOARD_UART_MINI        0
#define BOARD_UART_SAMSUNG     0
#define BOARD_UART_BASE        0x0ULL
#define BOARD_UART_BAUD        115200

#define BOARD_LINUX_IMAGE_HEADER 1
/* 对表 chiron 线刷 boot.img：kernel_addr=0x8000 → phys 0x80008000；gzip 解压落点 */
#define BOARD_LINUX_IMAGE_TEXT_OFFSET 0x8000
#define BOARD_KERNEL_LOAD      0x80008000ULL
#define BOARD_RAM_BASE         0x80000000ULL
#define BOARD_RAM_SIZE         (6ULL * 1024ULL * 1024ULL * 1024ULL)

#define BOARD_HAS_FRAMEBUFFER  1
#define BOARD_HAS_BLOCK        0
#define BOARD_HAS_NET          0
#define BOARD_HAS_WIFI         0
#define BOARD_HAS_BLUETOOTH    0
#define BOARD_HAS_AUDIO        0
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0

/*
 * fb0（PR-MIX2-1）：bpp=32 stride=4352（1088×4）。
 * aboot splash 用 4352 会整幅往右下斜 → 改 width×4=4320（真机对表）。
 */
#define BOARD_FRAMEBUFFER_WIDTH           1080
#define BOARD_FRAMEBUFFER_HEIGHT          2160
#define BOARD_FRAMEBUFFER_BITS_PER_PIXEL  32
#define BOARD_FRAMEBUFFER_STRIDE_BYTES    4320

/*
 * splash：
 *   线刷 boot DT / aboot cont_splash = 0x9D600000（PR-MIX2-2 画 logo 认这个）
 *   运行中 MIUI DT reg 曾读到 0x9D400000（PR-MIX2-1）；开机阶段 MDSS 仍扫 0x9D600000
 */
#define BOARD_SPLASH_PHYSICAL_BASE       0x9D600000ULL
#define BOARD_SPLASH_PHYSICAL_SIZE       0x02400000ULL
#define BOARD_SPLASH_PHYSICAL_BASE_ALT   0x9D400000ULL

#define BOARD_BRINGUP_IDENTITY_MAP 1
#define BOARD_BRINGUP_SPLASH       1
/* PR-MIX2-USB-1：探测 snps,dwc3 @0xA800000 */
#define BOARD_MIX2_USB_HOST_BRINGUP 1
#define BOARD_USB_DWC3_PHYSICAL_BASE 0x0A800000ULL

#endif /* BOARD_CONFIG_H */
