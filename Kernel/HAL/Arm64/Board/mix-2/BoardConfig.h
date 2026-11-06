/*
 * BoardConfig.h — 小米 MIX 2（chiron，MSM8998）
 *
 * 控制台：aboot 已扫的 cont_splash；不点亮 DSI、不做板上 UART。
 * PR-MIX2-1（2026-11-07）root 实测写入 bpp / stride / splash reg。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "mix-2"
#define BOARD_ARCH             "Arm64"

/* 本机无早期 UART 控制台；字画在 splash */
#define BOARD_UART_MINI        0
#define BOARD_UART_SAMSUNG     0

#define BOARD_LINUX_IMAGE_HEADER 1

/* PR-MIX2-2 再定加载址；此处只钉显示契约 */
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
 * fb0（root 读 /sys/class/graphics/fb0，MIUI V12.0.1.0.PDECNXM）：
 *   bits_per_pixel=32
 *   stride=4352（字节；1088 像素 × 4）
 *   modes=U:1080x2160p-60
 *   virtual_size=1080,4320
 */
#define BOARD_FRAMEBUFFER_WIDTH           1080
#define BOARD_FRAMEBUFFER_HEIGHT          2160
#define BOARD_FRAMEBUFFER_BITS_PER_PIXEL  32
#define BOARD_FRAMEBUFFER_STRIDE_BYTES    4352

/*
 * splash reserved-memory：
 *   节点名仍为 splash_region@9d600000，
 *   但 reg 实测为 base=0x9D400000 size=0x2400000（#address/#size-cells=2）。
 *   以 reg 为准，不抄节点 unit-address。
 */
#define BOARD_SPLASH_PHYSICAL_BASE  0x9D400000ULL
#define BOARD_SPLASH_PHYSICAL_SIZE  0x02400000ULL

#endif /* BOARD_CONFIG_H */
