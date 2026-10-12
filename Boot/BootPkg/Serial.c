/*
 * Serial.c — Boot 第 1 步：直接编程 COM1 上的 16550
 *
 * 谁调用：UefiMain 最先 BootSerialInitialize，紧接着 BootSerialBanner。
 * 其后各文件用 BootSerialPrintf 打进度和失败。
 *
 * 不使用 EFI_SERIAL_IO_PROTOCOL。QEMU 的 -serial stdio 接的就是
 * 端口 0x3F8 这块传统 UART；内核 Drivers/Serial.c 同一地址、同一 115200 8N1，
 * 日志才能从 Boot 接到内核。in/out 在 ExitBootServices 之后仍然合法，
 * UEFI 串口协议在告别固件之后不能再调。
 * 机器上若没有这块传统口（串口只挂在固件协议后面），探测失败，本文件全部静默。
 */
#include "Serial.h"
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>

#define BOOT_COM1 0x3F8u

/*
 * 16550 寄存器偏移。线路控制的 DLAB=1 时，+0/+1 不再是数据和中断使能，
 * 而是波特率除数的低字节和高字节。所以关中断必须发生在打开 DLAB 之前。
 */
#define UART_THR 0u
#define UART_IER 1u
/* 与上面两个同一偏移。只在 DLAB=1 时，+0/+1 才是除数低字节和高字节。 */
#define UART_DLL UART_THR
#define UART_DLM UART_IER
#define UART_FCR 2u
#define UART_LCR 3u
#define UART_MCR 4u
#define UART_LSR 5u
#define UART_SCR 7u

#define UART_LCR_DLAB 0x80u
#define UART_LCR_8N1  0x03u
/*
 * 使能 FIFO、清空收发队列、接收水位 14 字节。
 * 内核收数据时用 0x07（水位 1，离收满还有余地）。Boot 只发送，水位几乎无影响。
 */
#define UART_FCR_ENABLE_CLEAR_TRIG14 0xC7u
/* DTR|RTS|OUT2。OUT2 在 PC 上把中断线接到中断控制器；IER 仍为 0，并不开中断。 */
#define UART_MCR_DTR_RTS_OUT2 0x0Bu
#define UART_LSR_THRE 0x20u /* 发送保持寄存器空，可以写入下一字节 */
#define UART_LSR_TEMT 0x40u /* 发送移位寄存器也空了，这一行已经离开芯片 */
/* 参考钟 1.8432 MHz，先除 16 得 115200，除数 1 就是 115200 波特。 */
#define UART_DIV_115200_LO 0x01u
#define UART_DIV_115200_HI 0x00u

/*
 * 暂存格探测图案：0x55 = 01010101，0xAA = 10101010。
 * 不是 16550 的身份证，也不是引导扇区末尾那对 55 AA 签名。
 * 没有芯片时读口常常是 0xFF，两次读回对不上。
 */
#define UART_SCRATCH_A 0x55u
#define UART_SCRATCH_B 0xAAu

/* 真机和 USB 串口桥上 THR 可能很慢；以前 2000 次会盖掉还没发完的字节。 */
#define BOOT_SERIAL_WAIT 1000000

static int sSerialOk;

/*
 * GCC 扩展内联汇编：__asm__ volatile ( "模板" : 输出 : 输入 )。
 * AT&T 语法源在前、目的在后。"a" 把字节放在 AL。
 * "Nd"：端口小于 256 且是常数就写进指令，否则放进 DX。
 * COM1 在 0x3F8，超过 255，实际走 DX。volatile：不许删掉这条碰硬件的指令。
 */
static inline void BootSerialOut8(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

static inline UINT8 BootSerialIn8(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile ("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static void BootSerialWaitBit(UINT8 Mask) {
    int Timeout = BOOT_SERIAL_WAIT;

    while (Timeout-- && !(BootSerialIn8(BOOT_COM1 + UART_LSR) & Mask)) {
        __asm__ volatile ("pause");
    }
}

/* 偏移 +7 是暂存格，不参与收发。写两次、各读一次，都一致才认为有 UART。 */
static int BootSerialProbe(void) {
    UINT8 A;
    UINT8 B;

    BootSerialOut8(BOOT_COM1 + UART_SCR, UART_SCRATCH_A);
    A = BootSerialIn8(BOOT_COM1 + UART_SCR);
    BootSerialOut8(BOOT_COM1 + UART_SCR, UART_SCRATCH_B);
    B = BootSerialIn8(BOOT_COM1 + UART_SCR);
    return (A == UART_SCRATCH_A && B == UART_SCRATCH_B) ? 1 : 0;
}

void EFIAPI BootSerialInitialize(void) {
    sSerialOk = BootSerialProbe();
    if (!sSerialOk) {
        return;
    }
    /* 1. 关中断。DLAB 打开后 +1 变成除数高字节，再想关中断就写不到 IER 了。 */
    BootSerialOut8(BOOT_COM1 + UART_IER, 0x00);
    /* 2. 打开 DLAB，+0/+1 改为波特率除数。 */
    BootSerialOut8(BOOT_COM1 + UART_LCR, UART_LCR_DLAB);
    /* 3. 除数 1 = 115200。此时 +0/+1 是除数，不是数据和中断使能。 */
    BootSerialOut8(BOOT_COM1 + UART_DLL, UART_DIV_115200_LO);
    BootSerialOut8(BOOT_COM1 + UART_DLM, UART_DIV_115200_HI);
    /* 4. 8N1，并关上 DLAB。不关的话下一字节会改掉波特率。 */
    BootSerialOut8(BOOT_COM1 + UART_LCR, UART_LCR_8N1);
    /* 5. 打开并清空 FIFO。 */
    BootSerialOut8(BOOT_COM1 + UART_FCR, UART_FCR_ENABLE_CLEAR_TRIG14);
    /* 6. 口线就绪。放在最后，等波特率和帧格式稳定再让对面看见。 */
    BootSerialOut8(BOOT_COM1 + UART_MCR, UART_MCR_DTR_RTS_OUT2);
}

int EFIAPI BootSerialPresent(void) {
    return sSerialOk;
}

static void BootSerialPutChar(char C) {
    if (!sSerialOk) {
        return;
    }
    BootSerialWaitBit(UART_LSR_THRE);
    BootSerialOut8(BOOT_COM1 + UART_THR, (UINT8)C);
}

void EFIAPI BootSerialBanner(void) {
    /*
     * 探测失败时 PutChar 直接返回，写「COM1 Unavailable」也出不了芯片。
     * 此时屏幕还没设，Boot 在串口上就是静默的。
     */
    if (!BootSerialPresent()) {
        return;
    }
    BootSerialWrite("Blocks Boot\n");
    BootSerialWrite("COM1 Serial OK\n");
}

void EFIAPI BootSerialWrite(const char *Text) {
    if (!Text) {
        return;
    }
    while (*Text) {
        /*
         * AsciiVSPrint 会把格式串里的 '\n' 先扩成 "\r\n"。
         * 这里若再对 '\n' 补 '\r'，线上就是 "\r\r\n"：QEMU 多空一行，真机终端粘行。
         * 所以丢掉已经存在的 '\r'，只在单独的 '\n'（横幅这种直接 Write 的字符串）前补 '\r'。
         */
        if (*Text == '\r') {
            Text++;
            continue;
        }
        if (*Text == '\n') {
            BootSerialPutChar('\r');
            BootSerialPutChar('\n');
            BootSerialWaitBit(UART_LSR_TEMT);
        } else {
            BootSerialPutChar(*Text);
        }
        Text++;
    }
}

/* EFIAPI 必须：X64 上 VA_LIST 与 PrintLib（微软调用约定）对齐，否则 %d 等会读错参数。 */
void EFIAPI BootSerialPrintf(CONST CHAR8 *Fmt, ...) {
    CHAR8 Buf[256];
    VA_LIST Args;

    if (!Fmt) {
        return;
    }
    VA_START(Args, Fmt);
    AsciiVSPrint(Buf, sizeof(Buf), Fmt, Args);
    VA_END(Args);
    BootSerialWrite(Buf);
}
