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
/*
 * ABL 经 x0 交 DTB；无固定落点。占位满足 KernelHandoff，TryDtb 失败则 BOARD_RAM。
 */
#define BOARD_DTB_LOAD         0x90000000ULL
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
 * 面板：1080×2160。cont_splash stride=4320（=宽×4）；fb0 的 4352 会右下斜，勿混用。
 *
 * 水平相位（真机 ✅ 2026-11-08）：PHASE_X=592；左右缘无串色。
 *   链记：56→580→596（过）→590（欠）→592。非半屏对调。
 * 纵向：PHASE_Y=0。勿行 wrap（1684→全黑）。
 * 面板顶＝aboot 解锁锁标（不在 cont_splash 平面，清 0x9D600000 去不掉）。
 * 逻辑 (0,0)=缓冲左上＝锁标正下方（中上部起画彩条），不是屏中/下部。
 */
#define BOARD_FRAMEBUFFER_WIDTH           1080
#define BOARD_FRAMEBUFFER_HEIGHT          2160
#define BOARD_FRAMEBUFFER_BITS_PER_PIXEL  32
#define BOARD_FRAMEBUFFER_STRIDE_BYTES    4320
#define BOARD_FRAMEBUFFER_PHASE_X         592
#define BOARD_FRAMEBUFFER_PHASE_Y         0

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
