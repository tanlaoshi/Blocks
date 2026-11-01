/*
 * GpioEdgeCommands.c — gpio watch / edge / edgetake / edgeselftest
 *
 * watch：软件轮询电平变化（G5a，三架构都能跑，不依赖硬件边沿）。
 * edge / edgetake / edgeselftest：硬件锁存边沿（G5，Pi GPEDS / Duo INTSTATUS）。
 *
 * edgeselftest 判据（路线图 G5）：输出脚自己翻转 N 次，TakeEdgeEvent 恰好收到 N 次。
 *   不需要按键；1000 次是验收默认值。
 */
#include "ShellPrivate.h"
#include "GpioSyntax.h"
#include "GpioEdgeCommands.h"
#include "HalGpio.h"
#include "Hal.h"
#include "Console.h"

#define GPIO_WATCH_STEP_MILLISECONDS 5u
#define GPIO_EDGE_SELFTEST_DEFAULT_COUNT 1000u
/* 写引脚后回读几次再取事件：给边沿检测采样时间（见循环里的注释） */
#define GPIO_EDGE_SELFTEST_SETTLE_READS 8u

static void GpioPrintNamedResult(const char *Name, int ResultCode) {
    gConsole.Write("gpio: ");
    gConsole.Write(Name);
    gConsole.Write(ResultCode == 0 ? " ok\n" : " FAILED\n");
}

/*
 * watch：轮询式边沿监视（不依赖中断框架）
 *   为什么先做轮询：Pi 的 Arm64 侧 HalIrqRegister 还是空实现、GIC 也按 virt 布局写死，
 *   真中断要先补一整套中断分发（另开一刀）。轮询 5 ms 足够按键用，且三架构都能跑。
 */
void GpioCommandWatch(int Argc, char **Argv) {
    UINT32 Pin;
    UINT32 BudgetMilliseconds;
    UINT32 ElapsedMilliseconds = 0u;
    UINT32 Edges = 0u;
    int Previous = 0;
    int Current = 0;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 ||
        GpioParseNumber(GpioArg(Argc, Argv, GPIO_ARG_SECOND), &BudgetMilliseconds) != 0 ||
        BudgetMilliseconds == 0u) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioRead(Pin, &Previous) != 0) {
        gConsole.Write("gpio: watch FAILED (read)\n");
        return;
    }
    gConsole.Write("gpio: watch start, level=");
    gConsole.Write(Previous != 0 ? "1\n" : "0\n");
    while (ElapsedMilliseconds < BudgetMilliseconds) {
        HalDelayMilliseconds(GPIO_WATCH_STEP_MILLISECONDS);
        ElapsedMilliseconds += GPIO_WATCH_STEP_MILLISECONDS;
        if (HalGpioRead(Pin, &Current) != 0) {
            gConsole.Write("gpio: watch aborted (read failed)\n");
            return;
        }
        if (Current != Previous) {
            Edges++;
            gConsole.Write("gpio: edge -> ");
            gConsole.Write(Current != 0 ? "1\n" : "0\n");
            Previous = Current;
        }
    }
    gConsole.Write("gpio: watch done, edges=");
    gConsole.WriteHex32(Edges);
    gConsole.Write("\n");
}

/* edge <pin> rising|falling|both|off */
void GpioCommandEdge(int Argc, char **Argv) {
    UINT32 Pin;
    const char *Text = GpioArg(Argc, Argv, GPIO_ARG_SECOND);
    HAL_GPIO_EDGE Edge;

    if (GpioParsePin(Argc, Argv, &Pin) != 0 || Text == 0) {
        GpioWriteUsage();
        return;
    }
    if (GpioTextEqual(Text, "off") || GpioTextEqual(Text, "none")) {
        GpioPrintNamedResult("edge", HalGpioDisableEdgeDetect(Pin));
        return;
    }
    if (GpioTextEqual(Text, "rising")) {
        Edge = HAL_GPIO_EDGE_RISING;
    } else if (GpioTextEqual(Text, "falling")) {
        Edge = HAL_GPIO_EDGE_FALLING;
    } else if (GpioTextEqual(Text, "both")) {
        Edge = HAL_GPIO_EDGE_BOTH;
    } else {
        GpioWriteUsage();
        return;
    }
    GpioPrintNamedResult("edge", HalGpioEnableEdgeDetect(Pin, Edge));
}

/* edgetake <pin> —— 取走一次硬件锁存；fired=0/1 */
void GpioCommandEdgeTake(int Argc, char **Argv) {
    UINT32 Pin;
    int Fired = 0;

    if (GpioParsePin(Argc, Argv, &Pin) != 0) {
        GpioWriteUsage();
        return;
    }
    if (HalGpioTakeEdgeEvent(Pin, &Fired) != 0) {
        gConsole.Write("gpio: edgetake FAILED\n");
        return;
    }
    gConsole.Write("gpio: edgetake fired=");
    gConsole.Write(Fired != 0 ? "1\n" : "0\n");
}

/*
 * edgeselftest <pin> [count]
 *   输出模式自翻转 count 次（默认 1000），每次翻转后 Take 必须 fired=1。
 */
void GpioCommandEdgeSelfTest(int Argc, char **Argv) {
    UINT32 Pin;
    UINT32 Count = GPIO_EDGE_SELFTEST_DEFAULT_COUNT;
    UINT32 Index;
    UINT32 Got = 0u;
    int Level = 0;
    int Fired = 0;
    const char *CountText = GpioArg(Argc, Argv, GPIO_ARG_SECOND);

    if (GpioParsePin(Argc, Argv, &Pin) != 0) {
        GpioWriteUsage();
        return;
    }
    if (CountText != 0) {
        if (GpioParseNumber(CountText, &Count) != 0 || Count == 0u) {
            GpioWriteUsage();
            return;
        }
    }
    if (HalGpioSetMode(Pin, HAL_GPIO_MODE_OUTPUT) != 0 ||
        HalGpioWrite(Pin, 0) != 0) {
        gConsole.Write("gpio: edgeselftest FAILED (drive low)\n");
        return;
    }
    if (HalGpioEnableEdgeDetect(Pin, HAL_GPIO_EDGE_BOTH) != 0) {
        gConsole.Write("gpio: edgeselftest FAILED (enable edge)\n");
        return;
    }
    /* 清掉 Enable 前后可能留下的陈旧事件（有上限，防硬件异常死转） */
    for (Index = 0u; Index < 8u; Index++) {
        if (HalGpioTakeEdgeEvent(Pin, &Fired) != 0 || Fired == 0) {
            break;
        }
    }
    for (Index = 0u; Index < Count; Index++) {
        Level = (Level == 0) ? 1 : 0;
        if (HalGpioWrite(Pin, Level) != 0) {
            gConsole.Write("gpio: edgeselftest FAILED (write)\n");
            (void)HalGpioDisableEdgeDetect(Pin);
            return;
        }
        /*
         * 让边沿检测有时间采样：写引脚与读 GPEDS/INTSTATUS 之间必须隔开。
         * 真机症状：连翻 3 次只在第 3 次丢事件（前两次成功），即"写后立刻读"的竞态。
         * 这里做几次 APB 回读（比 1 ms 延迟便宜得多），对真实按键场景毫无影响。
         */
        for (UINT32 Settle = 0u; Settle < GPIO_EDGE_SELFTEST_SETTLE_READS; Settle++) {
            int Ignored = 0;
            (void)HalGpioRead(Pin, &Ignored);
        }
        if (HalGpioTakeEdgeEvent(Pin, &Fired) != 0 || Fired == 0) {
            UINT32 Snapshot[12];
            UINT32 SnapshotIndex;

            gConsole.Write("gpio: edgeselftest FAILED at toggle=");
            gConsole.WriteHex32(Index);
            gConsole.Write(" got=");
            gConsole.WriteHex32(Got);
            gConsole.Write("\n");
            if (HalGpioEdgeSnapshot(Pin, Snapshot) == 0) {
                gConsole.Write("gpio: edge snap DR/DDR/INTEN/MASK/TYPE/POL/ST/RAW/DEB/EXT/LS/Pin=");
                for (SnapshotIndex = 0u; SnapshotIndex < 12u; SnapshotIndex++) {
                    if (SnapshotIndex != 0u) {
                        gConsole.Write("/");
                    }
                    gConsole.WriteHex32(Snapshot[SnapshotIndex]);
                }
                gConsole.Write("\n");
            }
            (void)HalGpioDisableEdgeDetect(Pin);
            return;
        }
        Got++;
    }
    (void)HalGpioDisableEdgeDetect(Pin);
    gConsole.Write("gpio: edgeselftest PASS toggles=");
    gConsole.WriteHex32(Got);
    gConsole.Write("\n");
}

/* Gpio.c 已挂 irqtest 子命令；真中断路径未完，先占位以免三架构 link 断 */
void GpioCommandIrqTest(int Argc, char **Argv) {
    (void)Argc;
    (void)Argv;
    gConsole.Write("gpio: irqtest not implemented (need HalIrq path)\n");
}
