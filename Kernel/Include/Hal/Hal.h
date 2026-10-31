/*
 * Include/Hal.h — 硬件抽象层统一接口
 */
#ifndef HAL_H
#define HAL_H

#include "BootTypes.h"
#include "HalPort.h"
#include "HalSerial.h"
#include "HalVideo.h"
#include "HalConsole.h"
#include "HalDevices.h"
#include "HalBluetooth.h"
#include "HalPageTable.h"
#include "HalPat.h"
#include "HalPlatform.h"
#include "HalXhciFallback.h"
#include "HalRtc.h"

int HalInitialize(void);

void HalCpuHalt(void);
void HalCpuPark(void);
void HalCpuReboot(void);
void HalCpuShutdown(void);
/* 真机：ACPI 电源键短按（需 AcpiPowerInit）；1=应关机 */
int HalPowerButtonPressed(void);

#include "HalIrq.h"
void HalCpuRelax(void); /* 自旋等待提示（x86：pause） */

void HalTimerInitialize(void);
void HalTimerSetInterval(UINT32 Milliseconds);
void HalTimerAck(void);
void HalTimerStart(void);

void HalInstallUserMode(void);
void HalSyscallInitialize(void);
/* 用户任务内核栈顶 → TSS.RSP0 / 等价结构（PR-A1；取代 Common 调 ArchSetRsp0） */
void HalKernelSetStack(UINT64 StackTop);

/*
 * PR-A11：用户虚址布局（Common 不写死 0x40000000）。
 * x86：代码 0x40000000；Arm/RiscV virt：躲开内核恒等（Arm 核 @0x40000000）→ 4GiB 起。
 */
UINT64 HalUserCodeVirt(void);
UINT64 HalUserStackVirt(void);
UINT64 HalUserStackSize(void);
UINT64 HalUserBrkMax(void);
UINT64 HalUserSoBase(void);
UINT64 HalUserVirtEnd(void);
/* PR-U-mmap：匿名映射区 [MmapBase, MmapEnd)；含于 VirtEnd */
UINT64 HalUserMmapBase(void);
UINT64 HalUserMmapEnd(void);
/* 内嵌极小用户映像：进 EL0/U + write/exit（x86 空；virt 冒烟） */
void HalUserSelfTest(void);

/*
 * PR-A2 / PR-A15：可移植任务帧 / 系统调用 ABI。
 * Common 用这些 API，不直接写 Cs/Ss/Rax 或 GDT 选择子字面量；
 * 指令指针 / 栈指针走 InstructionPointer / StackPointer 中立名。
 */
void HalFrameSetKernelEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 StackTop);
void HalFrameSetUserEntry(HAL_INTERRUPT_FRAME *F, UINT64 Entry, UINT64 UserStackTop);
/* PR-U-thread-2：用户 TLS 基（x86=FS，Arm64=TPIDR_EL0，RiscV=tp/X4） */
void HalTlsSetBase(UINT64 UserTlsBase);
void HalFrameSetTls(HAL_INTERRUPT_FRAME *F, UINT64 UserTlsBase);
void HalFrameCopy(HAL_INTERRUPT_FRAME *Dst, const HAL_INTERRUPT_FRAME *Src);
UINT64 HalFrameGetInstructionPointer(const HAL_INTERRUPT_FRAME *F);
UINT64 HalFrameSyscallNum(const HAL_INTERRUPT_FRAME *F);
UINT64 HalFrameGetArgument0(const HAL_INTERRUPT_FRAME *F);
UINT64 HalFrameGetArgument1(const HAL_INTERRUPT_FRAME *F);
UINT64 HalFrameGetArgument2(const HAL_INTERRUPT_FRAME *F);
void HalFrameSetReturn(HAL_INTERRUPT_FRAME *F, UINT64 Value);
void HalFrameSetReturn2(HAL_INTERRUPT_FRAME *F, UINT64 A, UINT64 B);
/*
 * PR-U-sig：把用户帧改成进入 handler(sig)。
 * 返回 1：调用方须把 *OutResumeIp 写入用户地址 *OutPushSp（再 HalFrameSetStackPointer）；
 * 返回 0：链路寄存器已保存返回点（Arm/RiscV），无需压栈；
 * 返回 -1：失败。
 */
int HalFrameSignalSetup(HAL_INTERRUPT_FRAME *F, UINT64 Handler, UINT64 Sig,
                        UINT64 *OutResumeIp, UINT64 *OutPushSp);
void HalFrameSetStackPointer(HAL_INTERRUPT_FRAME *F, UINT64 Sp);
void HalFrameSetArgument0(HAL_INTERRUPT_FRAME *F, UINT64 Value);
void HalFrameSetArgument1(HAL_INTERRUPT_FRAME *F, UINT64 Value);
UINT64 HalFrameGetStackPointer(const HAL_INTERRUPT_FRAME *F);
void HalFrameSetInstructionPointer(HAL_INTERRUPT_FRAME *F, UINT64 Ip);

struct HAL_INTERRUPT_FRAME;
UINT64 HalInterruptDispatch(struct HAL_INTERRUPT_FRAME *Frame);
void HalSchedulerEnter(struct HAL_INTERRUPT_FRAME *Frame);
void HalUserEnter(struct HAL_INTERRUPT_FRAME *Frame);

#include "HalIo.h"

/*
 * PR-A4：ELF 机器号与重定位分类（供 Core ElfArchRegister；Library 不调此处）。
 * 枚举值须与 Include/Library/Elf.h 的 ELF_RELOC_* 一致。
 */
UINT16 HalElfMachine(void);

typedef enum {
    HAL_ELF_RELOC_UNSUPPORTED = 0,
    HAL_ELF_RELOC_RELATIVE,
    HAL_ELF_RELOC_ABS64,
    HAL_ELF_RELOC_GLOB_DAT,
    HAL_ELF_RELOC_JUMP_SLOT,
    HAL_ELF_RELOC_COPY
} HAL_ELF_RELOC_KIND;

HAL_ELF_RELOC_KIND HalElfRelocKind(UINT32 Type);

/* PR-A12：装载可执行页后同步 I-cache（x86 空；Arm/RiscV 必需） */
void HalSyncICache(void *Addr, UINTN Size);
/* Arm64：dc civac 范围；真机 FB/DMA 写后须调用，否则 VC/设备见脏 cache */
void HalDcacheCleanInvalidateRange(void *Va, UINTN Len);

/*
 * PR-A12：virt 无抢占时，从内核任务协作进入用户帧并在 exit 时返回。
 * 实现复用 SelfTestEnter/Return（Arm/RiscV）；x86 不调用。
 */
void HalUserCoopEnter(UINT64 Ksp, struct HAL_INTERRUPT_FRAME *Frame);
void HalUserCoopReturn(void);

const char *HalArchName(void);
const char *HalCpuInfo(void);

/*
 * PR-B1 能力旗标：Common 用能力判断，勿用「非 x86 = virt 串口」。
 * HalHasFrameBuffer — BOOT_HANDOFF 有可用帧缓冲（亮屏 / 桌面模块前提）
 * HalConsoleOnly    — 串口命令行靶：Virt 模块表、不 Create Gui/Worker/Input
 * HalPinInteractiveToBootstrap — 1→Shell/Gui/Input 全钉 BSP（盖过 HAL_INPUT_CPU_MIN）
 * 二者与 Input 门槛分域见 input-f3-console（Scheduler/Start.c / HalPlat.c）；勿跟 F2 缠改。
 */
int HalHasFrameBuffer(void);
int HalConsoleOnly(void);
int HalPinInteractiveToBootstrap(void);

/* 轮询时钟（virt 无 IRQ 时由 IdleLoop 调用；x86 可为空） */
void HalTimerPoll(void);
/* 1=定时器走 IRQ（CondResched→hlt→OnTimer）；0=仅 CNTV poll（Pi 真机） */
int HalTimerUsesIrq(void);
/* CPUID.1 ECX.31：QEMU/KVM 等为 1，裸机多为 0 */
int HalCpuIsHypervisor(void);

/* SMP：Common 只依赖这些门面；x86=MADT/SIPI，virt Arm/RiscV=PSCI/HSM（A14） */
#define HAL_MAX_CPUS 8
/*
 * Input 专核门槛（input-f2-threshold）：HalCpuCount()≥此值才 Create Input@CPU2。
 * 五门闩同此常数：KernelTrySpawnInput / YieldForPollInput / TasksPumpKeyboard /
 * XHCI InputIrqCpu / SchedulerStart 钉核。改数只改这里（原散落 <3/<=2/>2）。
 * 4=对齐 1/2/4 核产品：三核及以下 Yield/Hid 自 drain；≥4 才分出 Input 槽。
 */
#define HAL_INPUT_CPU_MIN 4

int HalCpuCount(void);
UINT32 HalCpuGetId(void);          /* 逻辑 CPU：0=BSP，1..N-1=AP */
/*
 * 写回逻辑 CPU id（CoopDrain 宿主恢复 / TLS 污染后钳正）。
 * RiscV=tp；Arm64=tpidr_el1；x86 由 LAPIC 推导，空操作。
 */
void HalCpuSetLogicalId(UINT32 Id);
UINT8 HalCpuApicId(UINT32 LogicalCpu); /* x86：LAPIC ID；其它 arch：0 */
int HalCpuIsBootstrapProcessor(void);
UINT64 HalCpuTicks(UINT32 Cpu); /* 每核 LAPIC/定时器原始拍 */
/*
 * LAPIC 每秒拍数（启发式）：QEMU≈20000（~50µs），NUC≈250（~4ms，按半秒验收反推）。
 * sleep / clock_ms = 拍 ×1000 / 此值；桌面双击仍用裸 HalCpuTicks。
 */
UINT32 HalTicksPerSec(void);
/* 单调毫秒（忙等/协议超时用；非墙钟） */
UINT32 HalMilliseconds(void);
void HalDelayMilliseconds(UINT32 Milliseconds);
void HalCpuIncrementTicks(void);
int HalSmpStartApplicationProcessors(void);
/* Startup 记下 DTB，供 DtbCpuCount（RiscV 地址可变；Arm 可回退固定 loader 址） */
void HalSmpNoteDtb(UINT64 DtbPhys);

void HalDebugWrite(const char *Text);
void HalDebugWriteHex32(UINT32 Value);
void HalDebugHex64(UINT64 Value);

/*
 * PR-UI-ttf-fpu：内核 FPU/SSE 岛。默认编译仍 -mgeneral-regs-only。
 * 仅 HalFpuBegin…End 之间可跑带 float 的 TU（如日后 TTF 栅格）。
 * 非 x86：Begin 返回 0。IRQ 路径禁止 Begin。
 */
void HalFpuEnableThisCpu(void);
int HalFpuBegin(void); /* 1=已进岛 */
void HalFpuEnd(void);
int HalFpuOk(void);    /* 1=BSP 探针通过 */
int HalFpuSelfTest(void); /* 0=通过 */

#endif
