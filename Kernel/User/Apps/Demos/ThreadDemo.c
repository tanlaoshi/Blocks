#include <Blocks/Process.h>
/*
 * ThreadDemo.c — thr-5：§0.1 三条故事 + 多线程 fork 失败 / 单线程 fork 回归
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <Blocks/thread.h>

#define LOOPS 3000
#define BUF_N 8

static pthread_mutex_t GLock = PTHREAD_MUTEX_INITIALIZER;
static volatile int GCount;
static volatile int GReady;
static int GBuf[BUF_N];
static volatile int GSum;

static void *IdWorker(void *Arg) {
    (void)Arg;
    printf("demo:id worker pid=%d tid=%d\n", (int)ProcessGetPid(),
           (int)ThreadGetTid());
    return 0;
}

static void *CountWorker(void *Arg) {
    int UseLock = (int)(long)Arg;
    int i;

    for (i = 0; i < LOOPS; i++) {
        if (UseLock) {
            pthread_mutex_lock(&GLock);
            GCount++;
            pthread_mutex_unlock(&GLock);
        } else {
            GCount++;
        }
    }
    return 0;
}

static void *ProdWorker(void *Arg) {
    int i;
    (void)Arg;

    pthread_mutex_lock(&GLock);
    for (i = 0; i < BUF_N; i++) {
        GBuf[i] = (i + 1) * 10;
    }
    GReady = 1;
    pthread_mutex_unlock(&GLock);
    return 0;
}

static void *ConsWorker(void *Arg) {
    int i;
    int Sum = 0;
    (void)Arg;

    for (;;) {
        pthread_mutex_lock(&GLock);
        if (GReady) {
            for (i = 0; i < BUF_N; i++) {
                Sum += GBuf[i];
            }
            GSum = Sum;
            pthread_mutex_unlock(&GLock);
            break;
        }
        pthread_mutex_unlock(&GLock);
        ThreadYield();
    }
    return 0;
}

static int StoryIdentity(void) {
    long T;
    pid_t Pid = ProcessGetPid();
    long MainTid = ThreadGetTid();

    printf("demo:id main pid=%d tid=%d\n", (int)Pid, (int)MainTid);
    T = ThreadCreate(IdWorker, 0);
    if (T < 0) {
        printf("demo:id create fail\n");
        return 1;
    }
    if (ThreadJoin(T, 0) != 0) {
        printf("demo:id join fail\n");
        return 1;
    }
    printf("demo:id ok (same pid, different tid)\n");
    return 0;
}

static int StoryCounter(void) {
    long A;
    long B;
    int Expect = LOOPS * 2;

    GCount = 0;
    A = ThreadCreate(CountWorker, (void *)(long)0);
    B = ThreadCreate(CountWorker, (void *)(long)0);
    if (A < 0 || B < 0) {
        printf("demo:count create fail\n");
        return 1;
    }
    ThreadJoin(A, 0);
    ThreadJoin(B, 0);
    printf("demo:count unlocked=%d expect=%d%s\n", GCount, Expect,
           (GCount == Expect) ? " (race may hide on 1 CPU)" : " (race shown)");

    GCount = 0;
    A = ThreadCreate(CountWorker, (void *)(long)1);
    B = ThreadCreate(CountWorker, (void *)(long)1);
    if (A < 0 || B < 0) {
        printf("demo:count create2 fail\n");
        return 1;
    }
    ThreadJoin(A, 0);
    ThreadJoin(B, 0);
    printf("demo:count locked=%d expect=%d\n", GCount, Expect);
    if (GCount != Expect) {
        printf("demo:count FAIL\n");
        return 1;
    }
    printf("demo:count ok\n");
    return 0;
}

static int StoryPipeline(void) {
    long P;
    long C;
    int Expect = 10 + 20 + 30 + 40 + 50 + 60 + 70 + 80;

    GReady = 0;
    GSum = 0;
    C = ThreadCreate(ConsWorker, 0);
    P = ThreadCreate(ProdWorker, 0);
    if (C < 0 || P < 0) {
        printf("demo:pipe create fail\n");
        return 1;
    }
    ThreadJoin(P, 0);
    ThreadJoin(C, 0);
    printf("demo:pipe sum=%d expect=%d\n", GSum, Expect);
    if (GSum != Expect) {
        printf("demo:pipe FAIL\n");
        return 1;
    }
    printf("demo:pipe ok\n");
    return 0;
}

/* join 前组内 alive>1 → ProcessFork 应 -EAGAIN */
static void *NopWorker(void *Arg) {
    (void)Arg;
    return 0;
}

static int StoryForkMulti(void) {
    long T;
    pid_t Pid;

    T = ThreadCreate(NopWorker, 0);
    if (T < 0) {
        printf("demo:forkm create fail\n");
        return 1;
    }
    /* worker 可能已退出成 zombie；组内 alive 仍 >1 直到 join */
    Pid = ProcessFork();
    if (Pid >= 0) {
        printf("demo:forkm FAIL: fork ok pid=%d (want fail)\n", (int)Pid);
        ThreadJoin(T, 0);
        return 1;
    }
    printf("demo:forkm ok (fork=%d)\n", (int)Pid);
    if (ThreadJoin(T, 0) != 0) {
        printf("demo:forkm join fail\n");
        return 1;
    }
    return 0;
}

static int StoryForkSingle(void) {
    pid_t Pid;
    int St = 0;

    Pid = ProcessFork();
    if (Pid < 0) {
        printf("demo:fork1 FAIL: fork=%d\n", (int)Pid);
        return 1;
    }
    if (Pid == 0) {
        printf("demo:fork1 child pid=%d\n", (int)ProcessGetPid());
        exit(7);
    }
    if (ProcessWait(&St) < 0) {
        printf("demo:fork1 wait fail\n");
        return 1;
    }
    printf("demo:fork1 parent child=%d st=%d\n", (int)Pid, WEXITSTATUS(St));
    if (WEXITSTATUS(St) != 7) {
        printf("demo:fork1 FAIL status\n");
        return 1;
    }
    printf("demo:fork1 ok\n");
    return 0;
}

int main(void) {
    printf("threaddemo: start pid=%d tid=%d\n", (int)ProcessGetPid(),
           (int)ThreadGetTid());
    if (StoryIdentity() != 0) {
        return 1;
    }
    if (StoryCounter() != 0) {
        return 1;
    }
    if (StoryPipeline() != 0) {
        return 1;
    }
    if (StoryForkMulti() != 0) {
        return 1;
    }
    if (StoryForkSingle() != 0) {
        return 1;
    }
    printf("threaddemo: ok\n");
    return 0;
}
