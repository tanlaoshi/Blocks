/*
 * Gpio.c — shell 命令 gpio：契约自检 + 手动读写（三架构共编）
 *
 * 子命令：
 *   gpio list                      本板可用引脚数与板名
 *   gpio mode <pin> in|out
 *   gpio pull <pin> none|up|down
 *   gpio write <pin> 0|1
 *   gpio read <pin>
 *   gpio selftest <pin>            ★ 判据：输出高→读回 1；输出低→读回 0（无需外设）
 *
 * 为什么 selftest 是这刀的核心：它把"驱动写对了"变成"引脚真的动了"，
 * 而且不需要任何外接器件 —— 输出模式下的读回电平来自引脚本身（Pi 走 GPLEV，
 * Duo 走 EXT_PORTA），所以接线松了/复用没配对都会 FAIL。
 *
 * 别改：selftest 失败时不得打印 PASS；参数错必须回 usage。
 */
#include "ShellPrivate.h"
#include "HalGpio.h"
#include "Console.h"

#define GPIO_ARG_COMMAND  1
#define GPIO_ARG_FIRST    2
#define GPIO_ARG_SECOND   3
#define GPIO_TEXT_MAX     32

static void GpioWriteUsage(void) {
    gConsole.Write("gpio list | mode <pin> in|out | pull <pin> none|up|down | "
                   "write <pin> 0|1 | read <pin> | selftest <pin>\n");
}

static const char *GpioArg(int Argc, char **Argv, int Index) {
    return (Index < Argc) ? Argv[Index] : 0;
}

/* 十进制/0x 十六进制都收（板号/引脚号手输，容错一点） */
static int GpioParseNumber(const char *Text, UINT32 *Out) {
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

static int GpioParsePin(int Argc, char **Argv, UINT32 *OutPin) {
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_FIRST);
    UINT32 Pin;

    if (GpioParseNumber(Text, &Pin) != 0) {
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

static void GpioPrintResult(const char *Name, int Rc) {
    gConsole.Write("gpio: ");
    gConsole.Write(Name);
    gConsole.Write(Rc == 0 ? " ok\n" : " FAILED\n");
}

static void GpioCommandList(void) {
    const char *Board = HalGpioBoardName();

    gConsole.Write("gpio: board=");
    gConsole.Write(Board != 0 ? Board : "unsupported");
    gConsole.Write(" pins=");
    gConsole.WriteHex32(HalGpioCount());
    gConsole.Write("\n");
}

static void GpioCommandMode(int Argc, char **Argv) {
    UINT32 Pin;
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_SECOND);
    HAL_GPIO_MODE Mode;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 || Text == 0) {
        GpioWriteUsage();
        return;
    }
    if (Text[0] == 'i' && Text[1] == 'n' && Text[2] == 0) {
        Mode = HAL_GPIO_MODE_INPUT;
    } else if (Text[0] == 'o' && Text[1] == 'u' && Text[2] == 't' && Text[3] == 0) {
        Mode = HAL_GPIO_MODE_OUTPUT;
    } else {
        GpioWriteUsage();
        return;
    }
    GpioPrintResult("mode", HalGpioSetMode(Pin, Mode));
}

static void GpioCommandPull(int Argc, char **Argv) {
    UINT32 Pin;
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_SECOND);
    HAL_GPIO_PULL Pull;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 || Text == 0) {
        GpioWriteUsage();
        return;
    }
    if (Text[0] == 'n') {
        Pull = HAL_GPIO_PULL_NONE;
    } else if (Text[0] == 'u') {
        Pull = HAL_GPIO_PULL_UP;
    } else if (Text[0] == 'd') {
        Pull = HAL_GPIO_PULL_DOWN;
    } else {
        GpioWriteUsage();
        return;
    }
    GpioPrintResult("pull", HalGpioSetPull(Pin, Pull));
}

static void GpioCommandWrite(int Argc, char **Argv) {
    UINT32 Pin;
    UINT32 Value;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 ||
        GpioParseNumber(GpioArg(Argc, Argv, GPIO_ARG_SECOND), &Value) != 0 || Value > 1u) {
        GpioWriteUsage();
        return;
    }
    GpioPrintResult("write", HalGpioWrite(Pin, (int)Value));
}

static void GpioCommandRead(int Argc, char **Argv) {
    UINT32 Pin;
    int High = 0;

    if (GpioParsePin(Argc, Argv, &Pin) != 0) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioRead(Pin, &High) != 0) {
        gConsole.Write("gpio: read FAILED\n");
        return;
    }
    gConsole.Write("gpio: pin ");
    gConsole.WriteHex32(Pin);
    gConsole.Write(High != 0 ? " = 1\n" : " = 0\n");
}

/*
 * 自检：不依赖任何外设。
 *   输出高 → 读回必须是 1；输出低 → 读回必须是 0。
 * 读回走的是引脚电平寄存器（不是输出影子寄存器），所以能证明"引脚真的动了"。
 */
static void GpioCommandSelfTest(int Argc, char **Argv) {
    UINT32 Pin;
    int High = 0;
    int Failures = 0;

    if (GpioParsePin(Argc, Argv, &Pin) != 0) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioSetMode(Pin, HAL_GPIO_MODE_OUTPUT) != 0) {
        gConsole.Write("gpio: selftest FAILED (set mode output)\n");
        return;
    }
    if (HalGpioWrite(Pin, 1) != 0 || HalGpioRead(Pin, &High) != 0 || High != 1) {
        gConsole.Write("gpio: selftest FAILED (drive high, read back != 1)\n");
        Failures++;
    }
    if (HalGpioWrite(Pin, 0) != 0 || HalGpioRead(Pin, &High) != 0 || High != 0) {
        gConsole.Write("gpio: selftest FAILED (drive low, read back != 0)\n");
        Failures++;
    }
    if (Failures == 0) {
        gConsole.Write("gpio: selftest PASS (drive high/low, read back matched)\n");
    }
}

void CommandGpio(int Argc, char **Argv) {
    const char *Name = GpioArg(Argc, Argv, GPIO_ARG_COMMAND);

    (void)GPIO_TEXT_MAX;
    if (Name == 0) {
        GpioWriteUsage();
        return;
    }
    if (Name[0] == 'l' && Name[1] == 'i') {
        GpioCommandList();
    } else if (Name[0] == 'm') {
        GpioCommandMode(Argc, Argv);
    } else if (Name[0] == 'p') {
        GpioCommandPull(Argc, Argv);
    } else if (Name[0] == 'w') {
        GpioCommandWrite(Argc, Argv);
    } else if (Name[0] == 'r') {
        GpioCommandRead(Argc, Argv);
    } else if (Name[0] == 's') {
        GpioCommandSelfTest(Argc, Argv);
    } else {
        GpioWriteUsage();
    }
}

void ShellCommandsGpioRegister(void) {
    gConsole.Register("gpio",
                      "gpio list | mode <pin> in|out | pull <pin> none|up|down | "
                      "write <pin> 0|1 | read <pin> | selftest <pin>",
                      CommandGpio);
}
