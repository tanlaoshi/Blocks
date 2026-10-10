/*
 * HalSyscall.c — int 0x80（K19 write/exit；K43 socket/connect/read/close）
 *
 * 【初学者】
 * - Hal/X64：用户 ELF 仍 ring0；ProcessFork 装向量 0x80。
 * - 入口：HalSyscallInitialize；ISR HalSyscallIsr。
 * - 边界：号表见文件内 SYS_*；不改 ABI 数字。
 */
#include "HalSyscall.h"
#include "HalCpu.h"
#include "HalSerial.h"
#include "ProcessFork.h"
#include "SyscallFile.h"
#ifdef HAVE_LWIP
#include "LwIpSock.h"
#endif

#define SYSCALL_VEC 0x80u
#define SYS_WRITE   1u
#define SYS_EXIT    2u
#define SYS_READ    3u
#define SYS_CLOSE   4u
#define SYS_SOCKET  5u
#define SYS_CONNECT 6u
#define SYS_FORK    7u
#define SYS_WAIT    8u
/* SYS_OPEN 见 SyscallFile.h (=9) */

/* 用户 fd：1=stdout；>=3 → sock id = fd-3 */
#define FD_SOCK_BASE 3

extern void HalSyscallIsr(void);

volatile int gUserDone;
UINT64 gSyscallRetRip;
UINT64 gSyscallRetRsp;

static int FdToSock(UINT64 Fd) {
    if (Fd < FD_SOCK_BASE) {
        return -1;
    }
    return (int)(Fd - FD_SOCK_BASE);
}

static void SysWrite(UINT64 *Regs) {
    UINT64 Fd = Regs[5];  /* rdi */
    UINT64 Buf = Regs[4]; /* rsi */
    UINT64 Len = Regs[3]; /* rdx */
    UINT64 i;
    int N;
#ifdef HAVE_LWIP
    int Sock;
#endif

    if (Fd == 1) {
        for (i = 0; i < Len && i < 4096u; i++) {
            char One[2];
            One[0] = ((const char *)(UINTN)Buf)[i];
            One[1] = 0;
            HalSerialWriteShell(One);
        }
        Regs[0] = Len;
        return;
    }
    if (SyscallFileIsFd(Fd)) {
        N = SyscallFileWrite((int)Fd, (const void *)(UINTN)Buf, (UINTN)Len);
        Regs[0] = N < 0 ? ~((UINT64)0) : (UINT64)(INT64)N;
        return;
    }
#ifdef HAVE_LWIP
    Sock = FdToSock(Fd);
    if (Sock < 0) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    N = LwIpSockSend(Sock, (const void *)(UINTN)Buf, (UINTN)Len);
    Regs[0] = N < 0 ? ~((UINT64)0) : (UINT64)(INT64)N;
#else
    (void)FdToSock;
    Regs[0] = ~((UINT64)0);
#endif
}

static void SysRead(UINT64 *Regs) {
    UINT64 Fd = Regs[5];
    UINT64 Buf = Regs[4];
    UINT64 Len = Regs[3];
    int N;
#ifdef HAVE_LWIP
    int Sock;
#endif

    if (SyscallFileIsFd(Fd)) {
        N = SyscallFileRead((int)Fd, (void *)(UINTN)Buf, (UINTN)Len);
        Regs[0] = N < 0 ? ~((UINT64)0) : (UINT64)(INT64)N;
        return;
    }
#ifdef HAVE_LWIP
    Sock = FdToSock(Fd);
    if (Sock < 0 || Buf == 0 || Len == 0) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    N = LwIpSockRecv(Sock, (void *)(UINTN)Buf, (UINTN)Len, 2000);
    if (N == -2) {
        Regs[0] = 0;
        return;
    }
    Regs[0] = N < 0 ? ~((UINT64)0) : (UINT64)(INT64)N;
#else
    (void)Buf;
    (void)Len;
    Regs[0] = ~((UINT64)0);
#endif
}

static void SysClose(UINT64 *Regs) {
    UINT64 Fd = Regs[5];
    int N;
#ifdef HAVE_LWIP
    int Sock;
#endif

    if (SyscallFileIsFd(Fd)) {
        N = SyscallFileClose((int)Fd);
        Regs[0] = N < 0 ? ~((UINT64)0) : 0;
        return;
    }
#ifdef HAVE_LWIP
    Sock = FdToSock(Fd);
    if (Sock < 0) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    Regs[0] = LwIpSockClose(Sock) == 0 ? 0 : ~((UINT64)0);
#else
    (void)Fd;
    Regs[0] = ~((UINT64)0);
#endif
}

static void SysOpen(UINT64 *Regs) {
    const char *Path = (const char *)(UINTN)Regs[5]; /* rdi */
    UINT64 Flags = Regs[4];                         /* rsi */
    int Fd = SyscallFileOpen(Path, Flags);
    Regs[0] = Fd < 0 ? ~((UINT64)0) : (UINT64)(UINT32)Fd;
}

static void SysSocket(UINT64 *Regs) {
#ifdef HAVE_LWIP
    UINT64 Domain = Regs[5];
    UINT64 Type = Regs[4];
    int Sock;

    (void)Regs[3];
    if (Domain != 2 || Type != 1) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    Sock = LwIpSockCreate();
    if (Sock < 0) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    Regs[0] = (UINT64)(FD_SOCK_BASE + Sock);
#else
    (void)Regs;
    Regs[0] = ~((UINT64)0);
#endif
}

static void SysConnect(UINT64 *Regs) {
#ifdef HAVE_LWIP
    UINT64 Fd = Regs[5];
    UINT32 Ip = (UINT32)Regs[4];
    UINT16 Port = (UINT16)Regs[3];
    int Sock;

    Sock = FdToSock(Fd);
    if (Sock < 0 || Port == 0) {
        Regs[0] = ~((UINT64)0);
        return;
    }
    Regs[0] = LwIpSockConnect(Sock, Ip, Port, 8000) == 0 ? 0 : ~((UINT64)0);
#else
    (void)Regs;
    Regs[0] = ~((UINT64)0);
#endif
}

void HalSyscallDispatch(UINT64 *Regs) {
    UINT64 Nr = Regs[0];

    if (Nr == SYS_WRITE) {
        SysWrite(Regs);
        return;
    }
    if (Nr == SYS_EXIT) {
        if (ProcessExitSyscall(Regs)) {
            return; /* 子结束 → 恢复父，继续 iretq */
        }
        gUserDone = 1;
        return;
    }
    if (Nr == SYS_READ) {
        SysRead(Regs);
        return;
    }
    if (Nr == SYS_CLOSE) {
        SysClose(Regs);
        return;
    }
    if (Nr == SYS_SOCKET) {
        SysSocket(Regs);
        return;
    }
    if (Nr == SYS_CONNECT) {
        SysConnect(Regs);
        return;
    }
    if (Nr == SYS_FORK) {
        (void)ProcessForkSyscall(Regs);
        return;
    }
    if (Nr == SYS_WAIT) {
        (void)ProcessWaitSyscall(Regs);
        return;
    }
    if (Nr == SYS_OPEN) {
        SysOpen(Regs);
        return;
    }
    Regs[0] = ~((UINT64)0);
}

int HalSyscallInitialize(void) {
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
