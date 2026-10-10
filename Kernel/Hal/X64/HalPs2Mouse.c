/*
 * HalPs2Mouse.c — X64 i8042 辅助口鼠标（对标现网 Ps2Aux + InputPs2）
 *
 * 【初学者】
 * HalPs2Poll：status→data→AUX 分流（与现网 Ps2Poll 同构）。
 * 组包进软件队列；Gui 只 Dequeue，Present 期靠 Poll 呼吸不丢字节。
 * Init 必须 FF→BAT→F4（ACK 认 AUX 位），否则 QEMU 冷启鼠常半死，多重启才活。
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

static void CpuRelax(void) {
    __asm__ volatile("pause");
}

static void BusyDelay(UINT32 N) {
    while (N-- > 0u) {
        CpuRelax();
    }
}

static void WaitIbClear(void) {
    UINT32 i;
    for (i = 0; i < 100000u; i++) {
        UINT8 St = In8(PS2_STATUS);
        if (St == 0xFFu || (St & PS2_IBF) == 0) {
            return;
        }
        CpuRelax();
    }
}

static int WaitObf(void) {
    UINT32 i;
    for (i = 0; i < 100000u; i++) {
        UINT8 St = In8(PS2_STATUS);
        if (St == 0xFFu) {
            return -1;
        }
        if (St & PS2_OBF) {
            return 0;
        }
        CpuRelax();
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

static void CtrlCmd(UINT8 Cmd) {
    WaitIbClear();
    Out8(PS2_STATUS, Cmd);
}

static void DataWrite(UINT8 V) {
    WaitIbClear();
    Out8(PS2_DATA, V);
}

/*
 * 读一字节。WantAux!=0 时优先 AUX；QEMU 上 Aux ACK 偶发不带 mouse 位，
 * 故 Init 路径用 WantAux=0，避免把 0xFA 当键盘字节扔掉导致 F4「失败」→ 无光标。
 */
static int AuxReadByte(UINT8 *Out, int WantAux) {
    int Guard = 64;
    UINT32 Spin;

    if (Out == 0) {
        return 0;
    }
    *Out = 0;
    while (Guard-- > 0) {
        for (Spin = 0; Spin < 200000u; Spin++) {
            UINT8 St = In8(PS2_STATUS);
            if (St == 0xFFu) {
                return 0;
            }
            if (St & PS2_OBF) {
                UINT8 B = In8(PS2_DATA);
                if (!WantAux || (St & PS2_AUX) != 0) {
                    *Out = B;
                    return 1;
                }
                /* 要 Aux 却收到键盘字节：丢弃再等 */
                break;
            }
            CpuRelax();
        }
    }
    return 0;
}

/* 对标现网 Ps2AuxWrite：D4+数据，ACK=0xFA，可重试 */
static int AuxWrite(UINT8 V) {
    int Try;
    UINT8 Ack;

    for (Try = 0; Try < 3; Try++) {
        DrainOb(32);
        CtrlCmd(0xD4u);
        DataWrite(V);
        Ack = 0;
        /* Init 期键盘已 AD：任意 OBF 的 FA 都算（兼容 QEMU 无 AUX 位） */
        if (AuxReadByte(&Ack, 0) && Ack == 0xFAu) {
            return 1;
        }
        if (Ack == 0xFEu) {
            continue;
        }
    }
    return 0;
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
        return; /* 满则丢本包，保实时 */
    }
    gQ[gQWr].Dx = Dx;
    gQ[gQWr].Dy = Dy;
    gQ[gQWr].Buttons = Btn;
    gQ[gQWr].Absolute = 0;
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
    /* 勿在包中途用 bit3 重同步：X/Y 位移也可能 bit3=1 */
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
    int Guard = 128;
    int GotAux = 0;

    while (Guard-- > 0) {
        UINT8 St = In8(PS2_STATUS);
        UINT8 B;

        if (St == 0xFFu || (St & PS2_OBF) == 0) {
            break;
        }
        B = In8(PS2_DATA);
        if (St & PS2_AUX) {
            GotAux = 1;
            MouseFeed(B);
        } else {
            HalPs2KeyboardFeed(B);
        }
    }
    /*
     * 停鼠半包（gPktN=1/2）必须按「无 Aux」计时；键盘字节不得清 stall，
     * 否则一停就卡到要对齐好几下。
     */
    if (!GotAux) {
        if (gPktN != 0u) {
            gPktStall++;
            if (gPktStall >= 2u) {
                gPktN = 0;
                gPktStall = 0;
            }
        }
    } else {
        gPktStall = 0;
    }
}

static int EnableStream(void) {
    if (!AuxWrite(0xF4u)) {
        return 0;
    }
    MouseResetStream();
    return 1;
}

/*
 * 对标现网 Ps2AuxInitDevice：关键盘口 → FF/BAT → F6 → F4；
 * 失败则 Drain 后仅 F4（BIOS/QEMU 可能已复位）。
 */
static int AuxInitDevice(void) {
    UINT8 Bat = 0;
    int GotReset = 0;

    CtrlCmd(0xADu); /* disable kbd */
    DrainOb(64);
    BusyDelay(80000u);

    CtrlCmd(0xA9u); /* aux interface test（参考） */
    (void)AuxReadByte(&Bat, 0);
    DrainOb(16);
    BusyDelay(80000u);

    if (AuxWrite(0xFFu)) {
        BusyDelay(300000u);
        if (AuxReadByte(&Bat, 0)) {
            if (Bat == 0xFAu) {
                (void)AuxReadByte(&Bat, 0);
            }
            if (Bat == 0xAAu) {
                GotReset = 1;
                (void)AuxReadByte(&Bat, 0); /* device id，可忽略 */
            }
        }
    }

    if (GotReset) {
        (void)AuxWrite(0xF6u);
        BusyDelay(60000u);
        if (EnableStream()) {
            return 1;
        }
    }

    /* 跳过 FF，直接开报告（QEMU/已复位设备常见） */
    DrainOb(32);
    BusyDelay(80000u);
    if (EnableStream()) {
        return 1;
    }
    /* 最后兜底：不问 ACK，再丢一次 F4，保证光标能开 */
    DrainOb(16);
    CtrlCmd(0xD4u);
    DataWrite(0xF4u);
    BusyDelay(40000u);
    DrainOb(16);
    MouseResetStream();
    return 1;
}

int HalPs2MouseInitialize(void) {
    UINT8 Cfg;

    gReady = 0;
    MouseResetStream();

    DrainOb(256);
    CtrlCmd(0xADu); /* disable kbd */
    CtrlCmd(0xA7u); /* disable aux */
    DrainOb(256);

    CtrlCmd(0xA8u); /* enable aux */

    CtrlCmd(0x20u);
    if (WaitObf() != 0) {
        CtrlCmd(0xAEu);
        return -1;
    }
    Cfg = In8(PS2_DATA);
    Cfg &= (UINT8)~(0x01u | 0x02u); /* 关 KBD/AUX IRQ，纯轮询 */
    Cfg &= (UINT8)~0x20u;           /* aux clock on */
    Cfg &= (UINT8)~0x10u;           /* kbd clock on */
    CtrlCmd(0x60u);
    DataWrite(Cfg);

    (void)AuxInitDevice();

    DrainOb(64);
    MouseResetStream();
    gReady = 1;

    CtrlCmd(0xAEu); /* 再开键盘口 */
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
