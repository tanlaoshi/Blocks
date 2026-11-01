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
#include "GpioSyntax.h"
#include "GpioEdgeCommands.h"
#include "HalGpio.h"
#include "Console.h"

static void GpioPrintResult(const char *Name, int Rc) {
    gConsole.Write("gpio: ");
    gConsole.Write(Name);
    gConsole.Write(Rc == 0 ? " ok\n" : " FAILED\n");
}

static void GpioCommandList(void) {
    const char *Board = HalGpioBoardName();
    const HAL_GPIO_BOARD_PIN *Pins;
    UINT32 Count = 0u;
    UINT32 Index;

    gConsole.Write("gpio: board=");
    gConsole.Write(Board != 0 ? Board : "unsupported");
    gConsole.Write(" pins=");
    gConsole.WriteHex32(HalGpioCount());
    gConsole.Write(" named=");
    Pins = HalGpioBoardPins(&Count);
    gConsole.WriteHex32(Count);
    gConsole.Write("\n");
    if (Pins == 0) {
        return;
    }
    for (Index = 0u; Index < Count; Index++) {
        gConsole.Write("  ");
        gConsole.Write(Pins[Index].Name != 0 ? Pins[Index].Name : "?");
        gConsole.Write(" gpio=");
        gConsole.WriteHex32(Pins[Index].Pin);
        gConsole.Write(" header=");
        gConsole.WriteHex32(Pins[Index].HeaderPin);
        gConsole.Write(Pins[Index].DefaultMode == HAL_GPIO_MODE_OUTPUT ? " out" : " in");
        gConsole.Write(Pins[Index].DefaultPull == HAL_GPIO_PULL_UP ? " pull-up" : "");
        gConsole.Write(Pins[Index].DefaultPull == HAL_GPIO_PULL_DOWN ? " pull-down" : "");
        gConsole.Write("\n");
    }
}

/* set：按板图默认模式配置后写值（最常用的一条，名字/数字都收） */
static void GpioCommandSet(int Argc, char **Argv) {
    UINT32 Pin;
    UINT32 Value;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 ||
        GpioParseNumber(GpioArg(Argc, Argv, GPIO_ARG_SECOND), &Value) != 0 || Value > 1u) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioSetMode(Pin, HAL_GPIO_MODE_OUTPUT) != 0) {
        gConsole.Write("gpio: set FAILED (mode)\n");
        return;
    }
    GpioPrintResult("set", HalGpioWrite(Pin, (int)Value));
}

/* init：把板图里的默认模式+上下拉写进硬件（只对具名脚有效） */
static void GpioCommandInit(int Argc, char **Argv) {
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_FIRST);
    UINT32 Pin = 0u;

    if (Text == 0) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioApplyNamedDefault(Text, &Pin) != 0) {
        gConsole.Write("gpio: init FAILED (unknown name, or board has no defaults)\n");
        return;
    }
    gConsole.Write("gpio: init ");
    gConsole.Write(Text);
    gConsole.Write(" ok\n");
}

/* get：读实际电平（名字/数字都收） */
static void GpioCommandGet(int Argc, char **Argv) {
    UINT32 Pin;
    int High = 0;

    if (GpioParsePin(Argc, Argv, &Pin) != 0) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioRead(Pin, &High) != 0) {
        gConsole.Write("gpio: get FAILED\n");
        return;
    }
    gConsole.Write("gpio: ");
    gConsole.Write(GpioArg(Argc, Argv, GPIO_ARG_FIRST));
    gConsole.Write(High != 0 ? " = 1\n" : " = 0\n");
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

    if (Name == 0) {
        GpioWriteUsage();
        return;
    }
    if (GpioTextEqual(Name, "list")) {
        GpioCommandList();
    } else if (GpioTextEqual(Name, "set")) {
        GpioCommandSet(Argc, Argv);
    } else if (GpioTextEqual(Name, "watch")) {
        GpioCommandWatch(Argc, Argv);
    } else if (GpioTextEqual(Name, "edge")) {
        GpioCommandEdge(Argc, Argv);
    } else if (GpioTextEqual(Name, "edgetake")) {
        GpioCommandEdgeTake(Argc, Argv);
    } else if (GpioTextEqual(Name, "edgeselftest")) {
        GpioCommandEdgeSelfTest(Argc, Argv);
    } else if (GpioTextEqual(Name, "init")) {
        GpioCommandInit(Argc, Argv);
    } else if (GpioTextEqual(Name, "get")) {
        GpioCommandGet(Argc, Argv);
    } else if (GpioTextEqual(Name, "mode")) {
        GpioCommandMode(Argc, Argv);
    } else if (GpioTextEqual(Name, "pull")) {
        GpioCommandPull(Argc, Argv);
    } else if (GpioTextEqual(Name, "write")) {
        GpioCommandWrite(Argc, Argv);
    } else if (GpioTextEqual(Name, "read")) {
        GpioCommandRead(Argc, Argv);
    } else if (GpioTextEqual(Name, "selftest")) {
        GpioCommandSelfTest(Argc, Argv);
    } else {
        GpioWriteUsage();
    }
}

void ShellCommandsGpioRegister(void) {
    gConsole.Register("gpio",
                      "gpio list|set|get|mode|pull|write|read|selftest|"
                      "watch|edge|edgetake|edgeselftest",
                      CommandGpio);
}
