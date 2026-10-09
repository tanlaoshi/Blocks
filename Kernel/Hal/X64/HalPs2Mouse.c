/*
 * HalPs2Mouse.c — X64 i8042 辅助口鼠标（K17）
 *
 * 【初学者】
 * 0xA8 开 AUX → 0xD4 转发命令给鼠 → 0xF4 开汇报。
 * 每包 3 字节：按钮/符号 | dX | dY（Y 向上为正，屏坐标要取反）。
 */
#include "HalPs2Mouse.h"

#define PS2_DATA    0x60u
#define PS2_STATUS  0x64u
#define PS2_OBF     (1u << 0)
#define PS2_IBF     (1u << 1)
#define PS2_AUX     (1u << 5)

static int gReady;
static UINT8 gBuf[3];
static UINT8 gN;

static void Out8(UINT16 Port, UINT8 Value) {
    __asm__ volatile("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

static UINT8 In8(UINT16 Port) {
    UINT8 Value;
    __asm__ volatile("inb %1, %0" : "=a"(Value) : "Nd"(Port));
    return Value;
}

static void WaitIbClear(void) {
    UINT32 i;
    for (i = 0; i < 100000u; i++) {
        if ((In8(PS2_STATUS) & PS2_IBF) == 0) {
            return;
        }
        __asm__ volatile("pause");
    }
}

static int WaitObf(void) {
    UINT32 i;
    for (i = 0; i < 100000u; i++) {
        if (In8(PS2_STATUS) & PS2_OBF) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

static void MouseWrite(UINT8 V) {
    WaitIbClear();
    Out8(PS2_STATUS, 0xD4u);
    WaitIbClear();
    Out8(PS2_DATA, V);
}

static int MouseReadAck(void) {
    if (WaitObf() != 0) {
        return -1;
    }
    return (In8(PS2_DATA) == 0xFAu) ? 0 : -1;
}

int HalPs2MouseInit(void) {
    UINT8 Cfg;
    UINT32 i;

    gReady = 0;
    gN = 0;

    WaitIbClear();
    Out8(PS2_STATUS, 0xA8u); /* enable aux */

    WaitIbClear();
    Out8(PS2_STATUS, 0x20u); /* read cfg */
    if (WaitObf() != 0) {
        return -1;
    }
    Cfg = In8(PS2_DATA);
    Cfg |= 0x02u;  /* enable aux IRQ（轮询也无妨） */
    Cfg &= (UINT8)~0x20u; /* clear aux clock disable */
    WaitIbClear();
    Out8(PS2_STATUS, 0x60u);
    WaitIbClear();
    Out8(PS2_DATA, Cfg);

    MouseWrite(0xF6u); /* defaults */
    (void)MouseReadAck();
    MouseWrite(0xF4u); /* enable reporting */
    if (MouseReadAck() != 0) {
        /* QEMU 有时仍可用；继续试 poll */
    }

    /* 排空残留 */
    for (i = 0; i < 64u; i++) {
        if ((In8(PS2_STATUS) & PS2_OBF) == 0) {
            break;
        }
        (void)In8(PS2_DATA);
    }
    gN = 0;
    gReady = 1;
    return 0;
}

int HalPs2MouseReady(void) {
    return gReady;
}

int HalPs2MousePoll(HAL_MOUSE_PACKET *Out) {
    UINT8 St;
    UINT8 B;

    if (!gReady || Out == 0) {
        return 0;
    }
    St = In8(PS2_STATUS);
    if ((St & PS2_OBF) == 0) {
        return 0;
    }
    if ((St & PS2_AUX) == 0) {
        return 0; /* 键盘字节留给 HalPs2Kbd */
    }
    B = In8(PS2_DATA);

    if (gN == 0) {
        /* bit3 应为 1；否则重新同步 */
        if ((B & 0x08u) == 0) {
            return 0;
        }
    }
    gBuf[gN++] = B;
    if (gN < 3u) {
        return 0;
    }
    gN = 0;

    Out->Buttons = (UINT8)(gBuf[0] & 0x07u);
    Out->Dx = (INT32)gBuf[1];
    Out->Dy = (INT32)gBuf[2];
    if (gBuf[0] & 0x10u) {
        Out->Dx |= (INT32)0xFFFFFF00; /* 符号扩展 9-bit 负 */
    }
    if (gBuf[0] & 0x20u) {
        Out->Dy |= (INT32)0xFFFFFF00;
    }
    /* PS/2 Y 向上为正；屏幕 Y 向下 */
    Out->Dy = -Out->Dy;
    return 1;
}
