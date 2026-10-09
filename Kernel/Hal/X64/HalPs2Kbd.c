/*
 * HalPs2Kbd.c — X64 i8042 键盘（Set1）；字节只由 HalPs2Poll 喂入
 *
 * 【初学者】
 * 禁止自己读 0x60 抢 OBF。HalPs2KbdFeed 由统一 Poll 调用（现网 Ps2KbdFeed）。
 */
#include "HalPs2Kbd.h"
#include "HalPs2.h"

#define PS2_DATA    0x60u
#define PS2_STATUS  0x64u
#define PS2_OBF     (1u << 0)
#define PS2_IBF     (1u << 1)

#define KBD_Q_CAP 32u

static int gReady;
static int gShiftL;
static int gShiftR;
static int gCaps;
static int gE0;
static char gQ[KBD_Q_CAP];
static UINT8 gQLen;

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

static int ShiftOn(void) {
    return gShiftL || gShiftR;
}

static void KbdQPush(char Ch) {
    if (gQLen >= KBD_Q_CAP || Ch == 0) {
        return;
    }
    gQ[gQLen++] = Ch;
}

static int KbdQPop(char *Out) {
    UINT8 i;
    if (gQLen == 0 || Out == 0) {
        return 0;
    }
    *Out = gQ[0];
    gQLen--;
    for (i = 0; i < gQLen; i++) {
        gQ[i] = gQ[i + 1u];
    }
    return 1;
}

/* 字母：Shift XOR Caps；其它符号只看 Shift */
static char Translate(UINT8 Code) {
    static const char Row1[] = "1234567890-=";
    static const char Row1s[] = "!@#$%^&*()_+";
    static const char Row2[] = "qwertyuiop[]";
    static const char Row2s[] = "QWERTYUIOP{}";
    static const char Row3[] = "asdfghjkl;'";
    static const char Row3s[] = "ASDFGHJKL:\"";
    static const char Row4[] = "zxcvbnm,./";
    static const char Row4s[] = "ZXCVBNM<>?";
    int Sh = ShiftOn();
    int Up;

    if (Code == 0x2Au) {
        gShiftL = 1;
        return 0;
    }
    if (Code == 0xAAu) {
        gShiftL = 0;
        return 0;
    }
    if (Code == 0x36u) {
        gShiftR = 1;
        return 0;
    }
    if (Code == 0xB6u) {
        gShiftR = 0;
        return 0;
    }
    if (Code == 0x3Au) {
        gCaps ^= 1;
        return 0;
    }
    if ((Code & 0x80u) != 0) {
        return 0;
    }
    if (Code == 0x1Cu) {
        return '\n';
    }
    if (Code == 0x0Eu) {
        return '\b';
    }
    if (Code == 0x39u) {
        return ' ';
    }
    if (Code >= 0x02u && Code <= 0x0Du) {
        return Sh ? Row1s[Code - 0x02u] : Row1[Code - 0x02u];
    }
    Up = Sh ^ gCaps;
    if (Code >= 0x10u && Code <= 0x19u) {
        return Up ? Row2s[Code - 0x10u] : Row2[Code - 0x10u];
    }
    if (Code == 0x1Au || Code == 0x1Bu) {
        return Sh ? Row2s[Code - 0x10u] : Row2[Code - 0x10u];
    }
    if (Code >= 0x1Eu && Code <= 0x26u) {
        return Up ? Row3s[Code - 0x1Eu] : Row3[Code - 0x1Eu];
    }
    if (Code >= 0x27u && Code <= 0x28u) {
        return Sh ? Row3s[Code - 0x1Eu] : Row3[Code - 0x1Eu];
    }
    if (Code >= 0x2Cu && Code <= 0x32u) {
        return Up ? Row4s[Code - 0x2Cu] : Row4[Code - 0x2Cu];
    }
    if (Code >= 0x33u && Code <= 0x35u) {
        return Sh ? Row4s[Code - 0x2Cu] : Row4[Code - 0x2Cu];
    }
    return 0;
}

void HalPs2KbdFeed(UINT8 Byte) {
    char Ch;

    if (!gReady) {
        return;
    }
    if (Byte == 0xE0u) {
        gE0 = 1;
        return;
    }
    if (gE0) {
        gE0 = 0;
        return;
    }
    Ch = Translate(Byte);
    if (Ch != 0) {
        KbdQPush(Ch);
    }
}

int HalPs2KbdInit(void) {
    gReady = 0;
    gShiftL = 0;
    gShiftR = 0;
    gCaps = 0;
    gE0 = 0;
    gQLen = 0;
    /* 鼠 Init 可能已开端口；此处只确保键盘口使能，勿大 Flush 吃 Aux */
    WaitIbClear();
    Out8(PS2_STATUS, 0xAEu);
    gReady = 1;
    return 0;
}

int HalPs2KbdReady(void) {
    return gReady;
}

int HalPs2KbdDiscardByte(void) {
    /* 兼容旧调用：统一走 Poll，不再私读 0x60 */
    HalPs2Poll();
    return 0;
}

int HalPs2KbdPollChar(char *Out) {
    if (!gReady || Out == 0) {
        return 0;
    }
    HalPs2Poll();
    return KbdQPop(Out);
}
