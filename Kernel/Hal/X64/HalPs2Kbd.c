/*
 * HalPs2Kbd.c — X64 i8042 键盘最小子集（K15）
 *
 * 【初学者】
 * 状态口 0x64：bit0=有数据可读，bit1=勿写。
 * 数据口 0x60：Set1 通码；高位 1=断码（松开）。
 * US ASCII；左/右 Shift + CapsLock → 大写与符号。
 */
#include "HalPs2Kbd.h"

#define PS2_DATA    0x60u
#define PS2_STATUS  0x64u
#define PS2_OBF     (1u << 0)
#define PS2_IBF     (1u << 1)
#define PS2_AUX     (1u << 5)

static int gReady;
static int gShiftL;
static int gShiftR;
static int gCaps;
static int gE0;

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

static void FlushOut(void) {
    UINT32 i;
    for (i = 0; i < 256u; i++) {
        if ((In8(PS2_STATUS) & PS2_OBF) == 0) {
            break;
        }
        (void)In8(PS2_DATA);
    }
}

static int ShiftOn(void) {
    return gShiftL || gShiftR;
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
    /* 字母行：Caps 参与 */
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

int HalPs2KbdInit(void) {
    gReady = 0;
    gShiftL = 0;
    gShiftR = 0;
    gCaps = 0;
    gE0 = 0;
    FlushOut();
    WaitIbClear();
    Out8(PS2_STATUS, 0x20u);
    {
        UINT32 i;
        for (i = 0; i < 100000u; i++) {
            if (In8(PS2_STATUS) & PS2_OBF) {
                (void)In8(PS2_DATA);
                break;
            }
            __asm__ volatile("pause");
        }
    }
    WaitIbClear();
    Out8(PS2_STATUS, 0xAEu);
    FlushOut();
    gReady = 1;
    return 0;
}

int HalPs2KbdReady(void) {
    return gReady;
}

int HalPs2KbdPollChar(char *Out) {
    UINT8 St;
    UINT8 Code;
    char Ch;

    if (!gReady || Out == 0) {
        return 0;
    }
    St = In8(PS2_STATUS);
    if ((St & PS2_OBF) == 0) {
        return 0;
    }
    if ((St & PS2_AUX) != 0) {
        return 0;
    }
    Code = In8(PS2_DATA);
    if (Code == 0xE0u) {
        gE0 = 1;
        return 0;
    }
    if (gE0) {
        gE0 = 0;
        /* 扩展键：右 Ctrl/Alt 等忽略；勿吞普通 Shift */
        return 0;
    }
    Ch = Translate(Code);
    if (Ch == 0) {
        return 0;
    }
    *Out = Ch;
    return 1;
}
