/*
 * HalPs2Kbd.c — X64 i8042 键盘最小子集（K15）
 *
 * 【初学者】
 * 状态口 0x64：bit0=有数据可读，bit1=勿写。
 * 数据口 0x60：Set1 通码；高位 1=断码（松开）忽略。
 * 只译一小段 US ASCII，够 Blocks> 打字。
 */
#include "HalPs2Kbd.h"

#define PS2_DATA    0x60u
#define PS2_STATUS  0x64u
#define PS2_OBF     (1u << 0)
#define PS2_IBF     (1u << 1)
#define PS2_AUX     (1u << 5)

static int gReady;
static int gShift;
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

/* Set1 通码 → ASCII；0 = 忽略 */
static char Translate(UINT8 Code) {
    static const char Row1[] = "1234567890-=";
    static const char Row1s[] = "!@#$%^&*()_+";
    static const char Row2[] = "qwertyuiop[]";
    static const char Row2s[] = "QWERTYUIOP{}";
    static const char Row3[] = "asdfghjkl;'";
    static const char Row3s[] = "ASDFGHJKL:\"";
    static const char Row4[] = "zxcvbnm,./";
    static const char Row4s[] = "ZXCVBNM<>?";

    if (Code == 0x2Au || Code == 0x36u) {
        gShift = 1;
        return 0;
    }
    if (Code == 0xAAu || Code == 0xB6u) {
        gShift = 0;
        return 0;
    }
    if ((Code & 0x80u) != 0) {
        return 0; /* 断码 */
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
        return gShift ? Row1s[Code - 0x02u] : Row1[Code - 0x02u];
    }
    if (Code >= 0x10u && Code <= 0x1Bu) {
        return gShift ? Row2s[Code - 0x10u] : Row2[Code - 0x10u];
    }
    if (Code >= 0x1Eu && Code <= 0x28u) {
        return gShift ? Row3s[Code - 0x1Eu] : Row3[Code - 0x1Eu];
    }
    if (Code >= 0x2Cu && Code <= 0x35u) {
        return gShift ? Row4s[Code - 0x2Cu] : Row4[Code - 0x2Cu];
    }
    return 0;
}

int HalPs2KbdInit(void) {
    gReady = 0;
    gShift = 0;
    gE0 = 0;
    FlushOut();
    WaitIbClear();
    /* 读控制器配置：能读到状态口即视为存在 */
    Out8(PS2_STATUS, 0x20u); /* read config byte */
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
    Out8(PS2_STATUS, 0xAEu); /* enable keyboard interface */
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
        return 0; /* 鼠标字节留给 HalPs2Mouse */
    }
    Code = In8(PS2_DATA);
    if (Code == 0xE0u) {
        gE0 = 1;
        return 0;
    }
    if (gE0) {
        gE0 = 0;
        return 0; /* 扩展键本刀忽略 */
    }
    Ch = Translate(Code);
    if (Ch == 0) {
        return 0;
    }
    *Out = Ch;
    return 1;
}
