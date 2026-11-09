/*
 * BoardConfig.h — Google Pixel 8（shiba，Tensor G3）
 *
 * 控制台：ABL 已扫的 cont_splash；不点亮 DSI、不做面板 init。
 * UART：ABL oem uart enable 后 @0x10870000（备份；本刀验收看屏）。
 * 加载：Android boot.img；内核 phys 以 ABL 实测为准。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_NAME             "pixel-8"
#define BOARD_ARCH             "Arm64"

#define BOARD_UART_MINI        0
#define BOARD_UART_SAMSUNG     1
#define BOARD_UART_BASE        0x10870000ULL
#define BOARD_UART_BAUD        115200

/* Linux aarch64 Image 头；ABL 当内核镜像加载 */
#define BOARD_LINUX_IMAGE_HEADER 1

/*
 * ABL 实测（串口 [AND] kernel: addr:0xffff000090701000 size:2004）：
 * LK 线性映 phys=0x90701000；size 0x2004=8196 即本 Image。
 * 旧抄 0x10008000 会让 adrp/栈错位；早期 '*' 虽绝对址写 UART，其后仍必对齐。
 */
#define BOARD_KERNEL_LOAD      0x90701000ULL
#define BOARD_DTB_LOAD         0x91F00000ULL
#define BOARD_RAM_BASE         0x80000000ULL
#define BOARD_RAM_SIZE         (8ULL * 1024ULL * 1024ULL * 1024ULL)

#define BOARD_HAS_FRAMEBUFFER  1
#define BOARD_HAS_BLOCK        0
#define BOARD_HAS_NET          0
#define BOARD_HAS_WIFI         0
#define BOARD_HAS_BLUETOOTH    0
#define BOARD_HAS_AUDIO        0
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0

/*
 * 面板：shiba 1080×2400。stride=宽×4（对表 gs101/zuma simple-fb 惯例）。
 * 水平相位 PHASE_X 先 0；真机左右串色再调（MIX2 曾调到 592）。
 */
#define BOARD_FRAMEBUFFER_WIDTH           1080
#define BOARD_FRAMEBUFFER_HEIGHT          2400
#define BOARD_FRAMEBUFFER_BITS_PER_PIXEL  32
#define BOARD_FRAMEBUFFER_STRIDE_BYTES    4320
#define BOARD_FRAMEBUFFER_PHASE_X         0
#define BOARD_FRAMEBUFFER_PHASE_Y         0

/*
 * splash：zuma ABL cont_splash @0xfac00000（husky 真机 DPP RDMA 同址；
 * shiba 同 SoC）。须先写 DECON0 TRIG_CON@0x19470030=0x3061（ABL 留 0x3070 遮罩）。
 * 长度取 16MiB（≥1080×2400×4）。
 */
#define BOARD_SPLASH_PHYSICAL_BASE       0xFAC00000ULL
#define BOARD_SPLASH_PHYSICAL_SIZE       0x01000000ULL

/* BRINGUP：静态恒等页表（内核窗 + UART Device + splash） */
#define BOARD_BRINGUP_IDENTITY_MAP 1
#define BOARD_BRINGUP_SPLASH       1

/*
 * USB Host：★=USB-2 hub；屏上看端口/e/a；坞直插 Type-C（不经 Cereal）。
 * 证据：device/google/zuma sys.usb.controller=11210000.dwc3
 */
#define BOARD_PIXEL8_USB_HOST_BRINGUP 1
#define BOARD_USB_DWC3_PHYSICAL_BASE  0x11210000ULL

#endif /* BOARD_CONFIG_H */
