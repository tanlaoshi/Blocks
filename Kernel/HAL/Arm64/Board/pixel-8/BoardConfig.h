/*
 * BoardConfig.h — Google Pixel 8（shiba，Tensor G3）
 *
 * UART：ABL oem uart enable 后 earlycon=exynos4210 @0x10870000 / ttySAC0 115200。
 * 加载：Android boot.img；内核物理址 base+0x80000 → 0x10008000（对表 factory mkbootimg）。
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

#define BOARD_HAS_FRAMEBUFFER  0
#define BOARD_HAS_BLOCK        0
#define BOARD_HAS_NET          0
#define BOARD_HAS_WIFI         0
#define BOARD_HAS_BLUETOOTH    0
#define BOARD_HAS_AUDIO        0
#define BOARD_CONSOLE_ONLY     1
#define BOARD_IS_VIRT          0

/* BRINGUP：静态恒等页表（内核窗 + UART Device） */
#define BOARD_BRINGUP_IDENTITY_MAP 1

#endif /* BOARD_CONFIG_H */
