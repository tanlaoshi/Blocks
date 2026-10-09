/*
 * HalPs2Mouse.c — X64 i8042 辅助口鼠标（对标现网 Ps2Aux + InputPs2）
 *
 * 【初学者】
 * HalPs2Poll：status→data→AUX 分流（与现网 Ps2Poll 同构）。
 * 组包进软件队列；Gui 只 Dequeue，Present 期靠 Poll 呼吸不丢字节。
 */
#include "HalPs2Mouse.h"
#include "HalPs2.h"

#define PS2_DATA    0x60u
#define PS2_STATUS  0x64u
#define PS2_OBF     (1u << 0)
#define PS2_IBF     (1u << 1)
#define PS2_AUX     (1u << 5)

#define MOUSE_Q     64u

static int gReady;
static UINT8 gPkt[3];
static UINT8 gPktN;
static UINT32 gPktStall;
static HAL_MOUSE_PACKET gQ[MOUSE_Q];
static UINT8 gQRd;
static UINT8 gQWr;

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

static void DrainOb(UINT32 Max) {
    UINT32 i;
    for (i = 0; i < Max; i++) {
        if ((In8(PS2_STATUS) & PS2_OBF) == 0) {
            return;
        }
        (void)In8(PS2_DATA);
    }
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

static void MouseResetStream(void) {
    gPktN = 0;
    gPktStall = 0;
    gQRd = 0;
    gQWr = 0;
}

static void MousePush(INT32 Dx, INT32 Dy, UINT8 Btn) {
    UINT8 Next = (UINT8)((gQWr + 1u) % MOUSE_Q);

    if (Next == gQRd) {
        return; /* 满则丢最旧策略：直接丢本包，保实时 */
    }
    gQ[gQWr].Dx = Dx;
    gQ[gQWr].Dy = Dy;
    gQ[gQWr].Buttons = Btn;
    gQWr = Next;
}

/* 对标现网 Ps2AuxFeed */
static void MouseFeed(UINT8 B) {
    INT32 Dx;
    INT32 Dy;

    if (!gReady) {
        return;
    }
    if (gPktN == 0) {
        if ((B & 0x08u) == 0) {
            return;
        }
        gPkt[0] = B;
        gPktN = 1;
        return;
    }
    gPkt[gPktN++] = B;
    if (gPktN < 3u) {
        return;
    }
    gPktN = 0;
    if (gPkt[0] & 0xC0u) {
        return;
    }
    Dx = (INT32)gPkt[1];
    Dy = (INT32)gPkt[2];
    if (gPkt[0] & 0x10u) {
        Dx -= 256;
    }
    if (gPkt[0] & 0x20u) {
        Dy -= 256;
    }
    /* PS/2 Y 向上为正；屏幕 Y 向下 */
    MousePush(Dx, -Dy, (UINT8)(gPkt[0] & 0x07u));
}

/* 对标现网 InputPs2.c Ps2Poll（勿 cli/sti：Present 热路径里会拖垮光标） */
void HalPs2Poll(void) {
    int Guard = 64;
    int GotByte = 0;

    while (Guard-- > 0) {
        UINT8 St = In8(PS2_STATUS);
        UINT8 B;

        if (St == 0xFFu || (St & PS2_OBF) == 0) {
            break;
        }
        GotByte = 1;
        B = In8(PS2_DATA);
        if (St & PS2_AUX) {
            MouseFeed(B);
        } else {
            HalPs2KbdFeed(B);
        }
    }
    /*
     * 停鼠时常停在半包（gPktN=1/2）→ 再动要对齐好几下像假死。
     * 空轮询数次后丢半包（Console 紧轮询下很快）。
     */
    if (!GotByte) {
        if (gPktN != 0u) {
            gPktStall++;
            if (gPktStall >= 32u) {
                gPktN = 0;
                gPktStall = 0;
            }
        }
    } else {
        gPktStall = 0;
    }
}

int HalPs2MouseInit(void) {
    UINT8 Cfg;

    gReady = 0;
    MouseResetStream();

    DrainOb(64);
    WaitIbClear();
    Out8(PS2_STATUS, 0xADu); /* 先关键盘口，避免抢 Aux ACK — 现网同序 */
    WaitIbClear();
    Out8(PS2_STATUS, 0xA8u); /* enable aux */

    WaitIbClear();
    Out8(PS2_STATUS, 0x20u);
    if (WaitObf() != 0) {
        WaitIbClear();
        Out8(PS2_STATUS, 0xAEu);
        return -1;
    }
    Cfg = In8(PS2_DATA);
    Cfg &= (UINT8)~(0x01u | 0x02u); /* 关 KBD/AUX IRQ，纯轮询 */
    Cfg &= (UINT8)~0x20u;           /* aux clock on */
    Cfg &= (UINT8)~0x10u;           /* kbd clock on */
    WaitIbClear();
    Out8(PS2_STATUS, 0x60u);
    WaitIbClear();
    Out8(PS2_DATA, Cfg);

    MouseWrite(0xF6u);
    (void)MouseReadAck();
    MouseWrite(0xF4u);
    (void)MouseReadAck();

    DrainOb(64);
    MouseResetStream();
    gReady = 1;

    WaitIbClear();
    Out8(PS2_STATUS, 0xAEu); /* 再开键盘口 */
    return 0;
}

int HalPs2MouseReady(void) {
    return gReady;
}

void HalPs2MouseDropInput(void) {
    /* 只排空硬件→队列，不清队列（对标现网：长 IO 里 Poll 不丢已入队事件） */
    HalPs2Poll();
}

int HalPs2MousePoll(HAL_MOUSE_PACKET *Out) {
    if (!gReady || Out == 0 || gQRd == gQWr) {
        return 0;
    }
    *Out = gQ[gQRd];
    gQRd = (UINT8)((gQRd + 1u) % MOUSE_Q);
    return 1;
}
