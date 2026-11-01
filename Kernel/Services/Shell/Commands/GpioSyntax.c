/*
 * GpioSyntax.c — gpio 命令的"语法层"：参数解析 + 用法文本
 *
 * 为什么单独一个文件：命令文件要守住 300 行原则；解析逻辑与动词实现混在一起会一直长。
 * 别改：名字优先于数字（HalGpioFindPin 先试）；解析失败返回 -1，绝不退化成 0 号脚。
 */
#include "GpioSyntax.h"
#include "HalGpio.h"
#include "Console.h"

void GpioWriteUsage(void) {
    gConsole.Write("gpio list | set <name|pin> 0|1 | get <name|pin> | "
                   "mode <pin> in|out | pull <pin> none|up|down | "
                   "write <pin> 0|1 | read <pin> | selftest <name|pin>\n");
}

const char *GpioArg(int Argc, char **Argv, int Index) {
    return (Index < Argc) ? Argv[Index] : 0;
}

/* 十进制/0x 十六进制都收（板号/引脚号手输，容错一点） */
int GpioParseNumber(const char *Text, UINT32 *Out) {
    UINT32 Value = 0u;
    int Digits = 0;

    if (Text == 0 || Out == 0) {
        return -1;
    }
    if (Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X')) {
        Text += 2;
        while (*Text != 0) {
            UINT32 Digit;
            if (*Text >= '0' && *Text <= '9') {
                Digit = (UINT32)(*Text - '0');
            } else if (*Text >= 'a' && *Text <= 'f') {
                Digit = (UINT32)(*Text - 'a') + 10u;
            } else if (*Text >= 'A' && *Text <= 'F') {
                Digit = (UINT32)(*Text - 'A') + 10u;
            } else {
                return -1;
            }
            Value = (Value << 4) | Digit;
            Digits++;
            Text++;
        }
    } else {
        while (*Text >= '0' && *Text <= '9') {
            Value = Value * 10u + (UINT32)(*Text - '0');
            Digits++;
            Text++;
        }
    }
    if (Digits == 0 || *Text != 0) {
        return -1;
    }
    *Out = Value;
    return 0;
}

/*
 * 引脚解析：先当名字查板图（LED/BTN），失败再当数字（17 / 0x11）。
 * 顺序不能反：名字里可能有数字开头的情况，将来也不许退化成"猜"。
 */
int GpioParsePinText(const char *Text, UINT32 *OutPin) {
    UINT32 Pin;

    if (Text == 0 || OutPin == 0) {
        return -1;
    }
    if (HalGpioFindPin(Text, &Pin) == 0) {
        *OutPin = Pin;
        return 0;
    }
    return GpioParseNumber(Text, OutPin);
}

int GpioParsePin(int Argc, char **Argv, UINT32 *OutPin) {
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_FIRST);
    UINT32 Pin;

    if (GpioParsePinText(Text, &Pin) != 0) {
        return -1;
    }
    if (Pin >= HalGpioCount()) {
        gConsole.Write("gpio: pin out of range (count=");
        gConsole.WriteHex32(HalGpioCount());
        gConsole.Write(")\n");
        return -1;
    }
    *OutPin = Pin;
    return 0;
}

/*
 * 完整匹配（不是前缀）。
 * 起因：真机上 `gpio selftest LED` 被前缀 's'+'e' 匹配成 `set`，直接打了 usage。
 * 规则：动词一律完整匹配；前缀匹配只允许出现在"没有公共前缀"的地方，这里一概不用。
 */
int GpioTextEqual(const char *Left, const char *Right) {
    if (Left == 0 || Right == 0) {
        return 0;
    }
    while (*Left != 0 && *Right != 0) {
        if (*Left != *Right) {
            return 0;
        }
        Left++;
        Right++;
    }
    return (*Left == 0 && *Right == 0) ? 1 : 0;
}
