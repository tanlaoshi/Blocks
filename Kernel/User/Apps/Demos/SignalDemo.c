#include <Blocks/thread.h>
#include <Blocks/Process.h>
/*
 * SigDemo.c — PR-U-sig：子进程安装 SIGTERM handler，父 kill 后 handler 运行并退出
 */
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "signal.h"
#include <sched.h>
#include <Blocks/syscall.h>

static volatile int gGot;

static void OnTerm(int Sig) {
    gGot = Sig;
    printf("sigdemo: handler sig=%d\n", Sig);
    exit(0);
}

int main(void) {
    pid_t Pid;
    int St;
    int I;

    Pid = ProcessFork();
    if (Pid < 0) {
        printf("sigdemo: fork fail\n");
        return 1;
    }
    if (Pid == 0) {
        if (signal(SIGTERM, OnTerm) == SIG_ERR) {
            printf("sigdemo: signal fail\n");
            return 1;
        }
        for (;;) {
            ThreadYield();
            if (gGot) {
                break;
            }
        }
        return 1;
    }

    printf("sigdemo: child pid=%d\n", (int)Pid);
    for (I = 0; I < 8; I++) {
        ThreadYield();
    }
    if (ProcessKill(Pid, SIGTERM) != 0) {
        printf("sigdemo: kill fail\n");
        return 1;
    }
    St = ProcessWait(0);
    if (St < 0) {
        printf("sigdemo: wait fail\n");
        return 1;
    }
    printf("sigdemo: ok\n");
    return 0;
}
