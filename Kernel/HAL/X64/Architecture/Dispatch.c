/*
 * Dispatch.c — x86-64 IDT / PIC / LAPIC / 中断分发（PR-S3-arch-1）
 *
 * 用户态异常（CS RPL=3）：杀当前用户任务并切走，勿 cli+hlt 拖死整机。
 * 内核异常仍 ExceptionHalt。
 */
#include "Compiler.h"
#include "Architecture.h"
#include "Private.h"
#include "Hal.h"
#include "Debug.h"
#include "Scheduler.h"
#include "Syscall.h"
#include "VirtualMemory.h"

extern void (*IsrException[32])(void);
extern void (*IsrPic[16])(void);
extern void Isr64(void);
extern void Isr65(void);
extern void Isr66(void);
extern void Isr67(void);
extern void Isr255(void);
void SerialClaimException(void);
void SerialWrite(const char *Text);
void SerialHexFormat(char *Buf, UINT64 Value, int Digits);

typedef struct {
    UINT16 Limit;
    UINT64 Base;
} BLOCKS_PACKED DT_PTR;

typedef struct {
    UINT16 OffLo;
    UINT16 Selector;
    UINT8  Ist;
    UINT8  Type;
    UINT16 OffMid;
    UINT32 OffHi;
    UINT32 Zero;
} BLOCKS_PACKED IDT_GATE;

static IDT_GATE gIdt[256] BLOCKS_ALIGNED(16);
volatile UINT32 gArchIrqCount;
static void (*gIrqNotify[256])(void);

void ArchIrqNotifyRegister(UINT32 Vector, void (*Handler)(void)) {
    if (Vector >= 256) {
        return;
    }
    gIrqNotify[Vector] = Handler;
}

/* 写 I/O 端口（字节） */
static inline void Outb(UINT16 Port, UINT8 Value) {
    __asm__ volatile ("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

/* 设置 IDT 门（Type: 0x8E=内核中断门, 0xEE=用户可调用中断门） */
void ArchIdtSetGate(UINT32 Vec, void *Handler, UINT8 Type) {
    UINT64 Addr = (UINT64)(UINTN)Handler;
    gIdt[Vec].OffLo = (UINT16)Addr;
    gIdt[Vec].Selector = 0x08;
    gIdt[Vec].Ist = 0;
    gIdt[Vec].Type = Type;
    gIdt[Vec].OffMid = (UINT16)(Addr >> 16);
    gIdt[Vec].OffHi = (UINT32)(Addr >> 32);
    gIdt[Vec].Zero = 0;
}

static void IdtSet(UINT32 Vec, void *Handler) {
    ArchIdtSetGate(Vec, Handler, 0x8E);
}

void ArchIdtLidt(void) {
    DT_PTR Ptr;
    Ptr.Limit = (UINT16)(sizeof(gIdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gIdt;
    __asm__ volatile ("lidt %0" : : "m"(Ptr) : "memory");
}

/* 填充 IDT：CPU 异常、PIC、自定义 XHCI/定时器向量，然后 lidt */
void ArchIdtLoad(void) {
    UINT32 i;
    for (i = 0; i < 32; i++) {
        IdtSet(i, (void *)IsrException[i]);
    }
    for (i = 32; i < 48; i++) {
        IdtSet(i, (void *)IsrPic[i - 32]);
    }
    for (i = 48; i < 256; i++) {
        IdtSet(i, (void *)Isr255);
    }
    IdtSet(VEC_XHCI, (void *)Isr64);
    IdtSet(VEC_TIMER, (void *)Isr65);
    IdtSet(VEC_E1000, (void *)Isr66);
    IdtSet(VEC_COM1, (void *)Isr67);
    IdtSet(255, (void *)Isr255);
    ArchIdtLidt();
}

/* 初始化并屏蔽 8259 双 PIC 所有 IRQ（使用 LAPIC 代替） */
void ArchPicMaskAll(void) {
    Outb(0x20, 0x11);
    Outb(0xA0, 0x11);
    Outb(0x21, 0x20);
    Outb(0xA1, 0x28);
    Outb(0x21, 0x04);
    Outb(0xA1, 0x02);
    Outb(0x21, 0x01);
    Outb(0xA1, 0x01);
    Outb(0x21, 0xFF);
    Outb(0xA1, 0xFF);
}

/* 向 8259 发送 EOI（从 PIC 兼容路径） */
static void PicEoi(UINT32 Irq) {
    if (Irq >= 8) {
        Outb(0xA0, 0x20);
    }
    Outb(0x20, 0x20);
}

#define LAPIC_BASE       0xFEE00000ULL
#define LAPIC_SVR        0xF0
#define LAPIC_EOI        0xB0
#define LAPIC_ICR_LO     0x300
#define LAPIC_ICR_HI     0x310
#define LAPIC_TPR        0x80
#define LAPIC_TIMER      0x320
#define LAPIC_TIMER_INIT 0x380
#define LAPIC_TIMER_DIV  0x3E0

/* 写 LAPIC EOI 寄存器，表示中断处理完毕 */
void LapicEoi(void) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_EOI) = 0;
}

/* 通过 IA32_APIC_BASE MSR 启用本地 APIC 并设置 SVR */
void ArchLapicEnable(void) {
    UINT64 ApicBase;
    __asm__ volatile ("rdmsr" : "=A"(ApicBase) : "c"(0x1B));
    if (!(ApicBase & (1ULL << 11))) {
        ApicBase |= (1ULL << 11);
        __asm__ volatile ("wrmsr" ::"c"(0x1B), "A"(ApicBase));
    }

    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TPR) = 0;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_SVR) = (1u << 8) | 0xFF;
}

/* 发送 LAPIC 自 IPI，用于启动时验证 IDT 是否工作 */
void ArchLapicSelfIpi(UINT8 Vector) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_ICR_HI) = 0;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_ICR_LO) =
        (1u << 18) | (1u << 14) | Vector;
}

/* 配置 LAPIC 周期定时器，中断向量 VEC_TIMER，初始计数 50000 */
void TimerStart(void) {
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER_DIV) = 0xB;
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER) =
        VEC_TIMER | (1u << 17);
    *(volatile UINT32 *)(UINTN)(LAPIC_BASE + LAPIC_TIMER_INIT) = 50000;
    DebugWrite("timer: LAPIC periodic vec=0x41\n");
}

/* 拼一段到 Line；超长截断（rm-exc-1：整行一次 SerialWrite） */
static void ExcAppend(char *Line, int Cap, int *Len, const char *Text) {
    if (!Line || !Len || !Text || Cap <= 0) {
        return;
    }
    while (*Text && *Len < Cap - 1) {
        Line[(*Len)++] = *Text++;
    }
    Line[*Len] = 0;
}

static void ExcAppendHex(char *Line, int Cap, int *Len, UINT64 Value, int Digits) {
    char Buf[24];

    SerialHexFormat(Buf, Value, Digits);
    ExcAppend(Line, Cap, Len, Buf);
}

static int FrameFromUser(const HAL_INTERRUPT_FRAME *F) {
    return F != 0 && (F->Cs & 3u) == 3u;
}

/* 拼 EXCEPTION 行（#PF 带 CR2）；调用方决定 Claim/停机或杀用户。 */
static void ExceptionFormat(HAL_INTERRUPT_FRAME *F, char *Line, int Cap, int *Len) {
    *Len = 0;
    ExcAppend(Line, Cap, Len, "\nEXCEPTION vec=");
    ExcAppendHex(Line, Cap, Len, (UINT32)F->Vector, 8);
    ExcAppend(Line, Cap, Len, " err=");
    ExcAppendHex(Line, Cap, Len, (UINT32)F->ErrorCode, 8);
    ExcAppend(Line, Cap, Len, " ip=");
    ExcAppendHex(Line, Cap, Len, F->InstructionPointer, 16);
    ExcAppend(Line, Cap, Len, " cs=");
    ExcAppendHex(Line, Cap, Len, F->Cs, 4);
    ExcAppend(Line, Cap, Len, " ss=");
    ExcAppendHex(Line, Cap, Len, F->Ss, 4);
    ExcAppend(Line, Cap, Len, " rsp=");
    ExcAppendHex(Line, Cap, Len, F->StackPointer, 16);
    ExcAppend(Line, Cap, Len, " rfl=");
    ExcAppendHex(Line, Cap, Len, F->Rflags, 8);
    ExcAppend(Line, Cap, Len, " cpu=");
    ExcAppendHex(Line, Cap, Len, (UINT32)HalCpuGetId(), 2);
    if (F->Vector == 14) {
        UINT64 Cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(Cr2));
        ExcAppend(Line, Cap, Len, " cr2=");
        ExcAppendHex(Line, Cap, Len, Cr2, 16);
    }
    ExcAppend(Line, Cap, Len, "\n");
}

/*
 * 内核异常：打印后 cli+hlt。
 * PR-K-rm-exc-1：先 SerialClaimException 独占 UART，再整行一次写出。
 */
static void ExceptionHalt(HAL_INTERRUPT_FRAME *F) {
    char Line[256];
    int Len = 0;

    ArchCli();
    SerialClaimException();
    ExceptionFormat(F, Line, (int)sizeof(Line), &Len);
    SerialWrite(Line);
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/*
 * 用户态异常：打日志后按 exit(139) 收尸切走（勿 Claim——Claim 会永久独占串口）。
 * hello/#PF 等不得拖死整机。
 */
static UINT64 ExceptionUserAbort(HAL_INTERRUPT_FRAME *F) {
    char Line[256];
    int Len = 0;

    ExceptionFormat(F, Line, (int)sizeof(Line), &Len);
    SerialWrite(Line);
    SerialWrite("fault: kill user (code 139), resume\n");
    HalFrameSetArgument0(F, 139);
    return SchedulerExitUser(F);
}

/*
 * 中断 C 分发入口（由 Interrupt.S 调用）
 * 返回值：0 表示不切换任务；非 0 为新任务 HAL_INTERRUPT_FRAME 指针（切换 RSP）
 */
UINT64 InterruptDispatch(HAL_INTERRUPT_FRAME *F) {
    if (F->Vector == 14) {
        UINT64 Cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(Cr2));
        if (VirtualMemoryHandlePageFault(Cr2, F->ErrorCode) == 0) {
            return 0;
        }
        if (FrameFromUser(F) && SchedulerIsOnline()) {
            return ExceptionUserAbort(F);
        }
        ExceptionHalt(F);
    }
    if (F->Vector < 32) {
        if (FrameFromUser(F) && SchedulerIsOnline()) {
            return ExceptionUserAbort(F);
        }
        ExceptionHalt(F);
    }
    if (F->Vector == VEC_XHCI || F->Vector == VEC_E1000 || F->Vector == VEC_COM1) {
        if (F->Vector == VEC_XHCI) {
            gArchIrqCount++;
        }
        if (F->Vector < 256 && gIrqNotify[F->Vector] != 0) {
            gIrqNotify[F->Vector]();
        }
        LapicEoi();
        return 0;
    }
    if (F->Vector == VEC_TIMER) {
        LapicEoi();
        HalCpuIncrementTicks();
        /* PR-S3：调度上线后所有核进 SchedulerOnTimer（大锁保护） */
        if (!SchedulerIsOnline()) {
            return 0;
        }
        return SchedulerOnTimer(F);
    }
    if (F->Vector == VEC_SYSCALL) {
        return SyscallDispatch(F);
    }
    if (F->Vector >= 32 && F->Vector < 48) {
        PicEoi((UINT32)(F->Vector - 32));
        LapicEoi();
        return 0;
    }
    LapicEoi();
    return 0;
}
