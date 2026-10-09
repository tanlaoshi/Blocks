/*
 * HalCpu.c — X64：最小 GDT + IDT（K7；K18 可改门）
 *
 * 【初学者】
 *   GDT：告诉 CPU「内核代码段 / 数据段」在哪（选择子 0x08 / 0x10）
 *   IDT：异常/IRQ 向量表；默认指向 halt stub，且本文件不 sti
 *   PIC：屏蔽 8259，避免残留 IRQ 捣乱
 *
 * LAPIC 定时器见 HalLapicTimer.c（HalCpuIdtSet 挂 0x20）。
 */
#include "HalCpu.h"
#include "BootTypes.h"

extern void HalCpuIsrHalt(void);

typedef struct {
    UINT16 Limit;
    UINT64 Base;
} __attribute__((packed)) DT_PTR;

typedef struct {
    UINT16 OffLo;
    UINT16 Selector;
    UINT8  Ist;
    UINT8  Type;
    UINT16 OffMid;
    UINT32 OffHi;
    UINT32 Zero;
} __attribute__((packed)) IDT_GATE;

/* null + kcode + kdata */
static UINT64 gGdt[3] __attribute__((aligned(16)));
static IDT_GATE gIdt[256] __attribute__((aligned(16)));

static inline void Outb(UINT16 Port, UINT8 Value) {
    __asm__ volatile("outb %0, %1" : : "a"(Value), "Nd"(Port));
}

static void PicMaskAll(void) {
    /* 初始化并屏蔽双 8259（与现网一致的最小套路） */
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

static void IdtSetGate(UINT32 Vec, void *Handler) {
    UINT64 Addr = (UINT64)(UINTN)Handler;

    gIdt[Vec].OffLo = (UINT16)Addr;
    gIdt[Vec].Selector = 0x08; /* 内核代码段 */
    gIdt[Vec].Ist = 0;
    gIdt[Vec].Type = 0x8E; /* 64-bit interrupt gate, DPL=0 */
    gIdt[Vec].OffMid = (UINT16)(Addr >> 16);
    gIdt[Vec].OffHi = (UINT32)(Addr >> 32);
    gIdt[Vec].Zero = 0;
}

static void GdtLoad(void) {
    DT_PTR Ptr;

    gGdt[0] = 0;
    gGdt[1] = 0x00AF9A000000FFFFULL; /* 64-bit code */
    gGdt[2] = 0x00CF92000000FFFFULL; /* data */

    Ptr.Limit = (UINT16)(sizeof(gGdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gGdt;

    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        :
        : "m"(Ptr)
        : "rax", "memory");
}

static void IdtLoad(void) {
    UINT32 i;
    DT_PTR Ptr;

    for (i = 0; i < 256; i++) {
        IdtSetGate(i, (void *)HalCpuIsrHalt);
    }
    Ptr.Limit = (UINT16)(sizeof(gIdt) - 1);
    Ptr.Base = (UINT64)(UINTN)gIdt;
    __asm__ volatile("lidt %0" : : "m"(Ptr) : "memory");
}

void HalCpuIdtSet(UINT32 Vec, void *Handler) {
    if (Vec < 256u && Handler != 0) {
        IdtSetGate(Vec, Handler);
    }
}

int HalCpuInitialize(void) {
    GdtLoad();
    PicMaskAll();
    IdtLoad();
    /* 不 sti：开中断由 HalTimer / Scheduler 决定 */
    return 0;
}
