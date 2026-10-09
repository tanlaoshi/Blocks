/*
 * HalSyscall.c — K19：int 0x80（write=1 / exit=2）
 *
 * 本刀用户 ELF 仍在 ring0 经 call 进入（课感：装载+系统调用）；真 ring3 后刀。
 */
#include "HalSyscall.h"
#include "HalCpu.h"
#include "HalSerial.h"

#define SYSCALL_VEC 0x80u
#define SYS_WRITE   1u
#define SYS_EXIT    2u

extern void HalSyscallIsr(void);

volatile int gUserDone;
UINT64 gSyscallRetRip;
UINT64 gSyscallRetRsp;

void HalSyscallDispatch(UINT64 *Regs) {
    UINT64 Nr = Regs[0]; /* rax */
    if (Nr == SYS_WRITE) {
        UINT64 Buf = Regs[4]; /* rsi */
        UINT64 Len = Regs[3]; /* rdx */
        UINT64 i;
        for (i = 0; i < Len && i < 4096u; i++) {
            char One[2];
            One[0] = ((const char *)(UINTN)Buf)[i];
            One[1] = 0;
            HalSerialWriteShell(One);
        }
        Regs[0] = Len;
        return;
    }
    if (Nr == SYS_EXIT) {
        gUserDone = 1;
        return;
    }
    Regs[0] = ~((UINT64)0);
}

int HalSyscallInit(void) {
    gUserDone = 0;
    HalCpuIdtSet(SYSCALL_VEC, (void *)HalSyscallIsr);
    return 0;
}

int HalSyscallRun(UINT64 Entry, UINT64 StackTop) {
    gUserDone = 0;
    __asm__ volatile(
        "lea 1f(%%rip), %%rax\n\t"
        "mov %%rax, %0\n\t"
        "mov %%rsp, %1\n\t"
        "mov %%rdx, %%rsp\n\t"
        "xor %%rbp, %%rbp\n\t"
        "call *%%rcx\n\t"
        "1:\n\t"
        "mov %1, %%rsp\n\t"
        : "+m"(gSyscallRetRip), "+m"(gSyscallRetRsp)
        : "c"(Entry), "d"(StackTop)
        : "rax", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory");
    return gUserDone ? 0 : -1;
}
