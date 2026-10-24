/*
 * Syscall.c — 系统调用分发（入口无关）
 *
 * 教学双路径（互不耦合）：
 *   - legacy：int 0x80 → IDT 门 → InterruptDispatch → SyscallDispatch
 *   - 快速：syscall → SyscallEntry → SyscallDispatch → sysretq
 * 号与 ABI（rax / rdi,rsi,rdx）两条路径共用。
 * 向量/MSR 安装在 HalSyscallInitialize（PR-A1），本文件只做分发。
 * 实现：FileSystem.c / Process.c（PR-S-syscall-split-1）。
 */
#include "Syscall.h"
#include "SyscallPrivate.h"
#include "Hal.h"
#include "SerialLog.h"
#include "Scheduler.h"
#include "Process.h"
#include "Errno.h"

void SyscallInitialize(void) {
    /* 硬件入口已由 HalSyscallInitialize 安装；保留符号供旧调用点 / 文档 */
}

UINT64 SyscallDispatch(HAL_INTERRUPT_FRAME *Frame) {
    UINT64 Ret = 0;
    static UINT32 UnknownLogs;

    /* 保持 IF=0 直到 iretq 恢复用户 RFLAGS，避免在返回路径嵌套定时器抢占 */
    HalIrqDisable();

    switch (HalFrameSyscallNum(Frame)) {
    case SYS_PROCESS_EXIT:
        Ret = SchedulerExitUser(Frame);
        break;
    case SYS_THREAD_CREATE:
        Ret = SchedulerThreadCreate(Frame);
        break;
    case SYS_THREAD_JOIN:
        Ret = SchedulerThreadJoin(Frame);
        break;
    case SYS_THREAD_EXIT:
        Ret = SchedulerThreadExit(Frame);
        break;
    case SYS_FILE_WRITE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysWrite(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            (UINTN)HalFrameGetArgument2(Frame)));
        break;
    case SYS_FILE_OPEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysOpen(HalFrameGetArgument0(Frame)));
        break;
    case SYS_FILE_READ:
        HalFrameSetReturn(Frame, (UINT64)(long)SysRead(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            (UINTN)HalFrameGetArgument2(Frame)));
        break;
    case SYS_FILE_CLOSE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysClose((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_FILE_SEEK:
        HalFrameSetReturn(Frame, (UINT64)SysLseek(
            (int)HalFrameGetArgument0(Frame),
            (INT64)HalFrameGetArgument1(Frame),
            (int)HalFrameGetArgument2(Frame)));
        break;
    case SYS_PROCESS_FORK:
        Ret = SchedulerFork(Frame);
        break;
    case SYS_PROCESS_WAIT:
        Ret = SchedulerWait(Frame);
        break;
    case SYS_THREAD_YIELD:
        Ret = SchedulerYield(Frame);
        break;
    case SYS_NET_SOCKET:
        HalFrameSetReturn(Frame, (UINT64)(long)SysSocket(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame),
            HalFrameGetArgument2(Frame)));
        break;
    case SYS_NET_CONNECT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysConnect(
            (int)HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT16)HalFrameGetArgument2(Frame)));
        break;
    case SYS_NET_BIND:
        HalFrameSetReturn(Frame, (UINT64)(long)SysBind(
            (int)HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT16)HalFrameGetArgument2(Frame)));
        break;
    case SYS_NET_LISTEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysListen(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame)));
        break;
    case SYS_NET_ACCEPT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysAccept((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_NET_RECV_NB:
        HalFrameSetReturn(Frame, (UINT64)(long)SysRecvNb(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            (UINTN)HalFrameGetArgument2(Frame)));
        break;
    case SYS_PROCESS_EXEC:
        if (SysExecve(Frame, HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
                      HalFrameGetArgument2(Frame)) != 0) {
            HalFrameSetReturn(Frame, (UINT64)(INT64)-1);
        }
        /* 成功：Frame 已指向新入口，sysret 进新映像 */
        break;
    case SYS_PROCESS_PIPE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysPipe(HalFrameGetArgument0(Frame)));
        break;
    case SYS_FILE_DUP:
        HalFrameSetReturn(Frame, (UINT64)(long)SysDup((int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_MEMORY_BRK:
        HalFrameSetReturn(Frame, ProcessBrk(HalFrameGetArgument0(Frame)));
        break;
    case SYS_PROCESS_KILL:
        Ret = SchedulerKill(Frame);
        break;
    case SYS_WINDOW_CREATE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysCreateWindow(
            HalFrameGetArgument0(Frame), (UINT32)HalFrameGetArgument1(Frame),
            (UINT32)HalFrameGetArgument2(Frame)));
        break;
    case SYS_WINDOW_DAMAGE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysDamage(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_GRAPHICS_DAMAGE_RECT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysDamageRect(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_WINDOW_POLL:
        HalFrameSetReturn(Frame, (UINT64)(long)SysPollInput(
            (int)HalFrameGetArgument0(Frame)));
        break;
    case SYS_UI_BUTTON:
        HalFrameSetReturn(Frame, (UINT64)(long)SysUiButton(
            (int)HalFrameGetArgument0(Frame), (int)HalFrameGetArgument1(Frame),
            HalFrameGetArgument2(Frame)));
        break;
    case SYS_UI_SCREEN_SIZE:
        HalFrameSetReturn(Frame, (UINT64)(long)SysScreenSize(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_UI_SET_FONT_ID:
        HalFrameSetReturn(Frame, (UINT64)(long)SysSetFontId(
            (UINT32)HalFrameGetArgument0(Frame)));
        break;
    case SYS_UI_FONT_COUNT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysFontCount());
        break;
    case SYS_FILE_STAT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysFileStat(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_FILE_FSTAT:
        HalFrameSetReturn(Frame, (UINT64)(long)SysFstat(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_DIR_OPEN:
        HalFrameSetReturn(Frame, (UINT64)(long)SysOpenDirectory(
            HalFrameGetArgument0(Frame)));
        break;
    case SYS_DIR_READ:
        HalFrameSetReturn(Frame, (UINT64)(long)SysReadDirectory(
            (int)HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_MEMORY_MAP:
        HalFrameSetReturn(Frame, ProcessMmap(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame),
            HalFrameGetArgument2(Frame)));
        break;
    case SYS_MEMORY_UNMAP:
        HalFrameSetReturn(Frame, ProcessMunmap(
            HalFrameGetArgument0(Frame), HalFrameGetArgument1(Frame)));
        break;
    case SYS_PROCESS_SIGNAL:
        Ret = SchedulerSignal(Frame);
        break;
    case SYS_TIME_SLEEP:
        Ret = SchedulerSleepMs(Frame, (UINT32)HalFrameGetArgument0(Frame));
        break;
    case SYS_PATH_GETCWD:
        HalFrameSetReturn(Frame, (UINT64)(long)SysGetcwd(
            HalFrameGetArgument0(Frame), (UINTN)HalFrameGetArgument1(Frame)));
        break;
    case SYS_PATH_CHDIR:
        HalFrameSetReturn(Frame, (UINT64)(long)SysChdir(HalFrameGetArgument0(Frame)));
        break;
    case SYS_PROCESS_GET_PID:
        HalFrameSetReturn(Frame, (UINT64)(long)SysGetPid());
        break;
    case SYS_PROCESS_GET_PPID:
        HalFrameSetReturn(Frame, (UINT64)(long)SysGetPpid());
        break;
    case SYS_THREAD_GET_TID:
        HalFrameSetReturn(Frame, (UINT64)(long)SysGetTid());
        break;
    case SYS_CLOCK_MS:
        {
            UINT32 Tps = HalTicksPerSec();

            if (Tps == 0) {
                Tps = 1000;
            }
            HalFrameSetReturn(Frame, HalCpuTicks(0) * 1000ULL / (UINT64)Tps);
        }
        break;
    case SYS_TASK_SNAP:
        HalFrameSetReturn(Frame, (UINT64)(long)SysTaskSnap(
            HalFrameGetArgument0(Frame), (UINTN)HalFrameGetArgument1(Frame)));
        break;
    default:
        /* 开课前冻结：未知号 → -ENOSYS；日志限次，避免每 tick 刷屏 */
        if (UnknownLogs < 8) {
            UnknownLogs++;
            LogMisc("syscall: unknown ");
            LogMiscHex64(HalFrameSyscallNum(Frame));
            LogMisc("\n");
            if (UnknownLogs == 8) {
                LogMisc("syscall: unknown (further suppressed)\n");
            }
        }
        HalFrameSetReturn(Frame, (UINT64)(INT64)(-(INT64)ENOSYS));
        break;
    }

    return Ret;
}
