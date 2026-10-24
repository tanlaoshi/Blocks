#include <Blocks/Process.h>
/*
 * ThreadSmoke.c — thr-3：create / join / gettid 冒烟
 */
#include <stdio.h>
#include <unistd.h>
#include <Blocks/thread.h>

static volatile int GFlag;

static void *Worker(void *Arg) {
    long N = (long)Arg;
    printf("worker: tid=%d arg=%d\n", (int)ThreadGetTid(), (int)N);
    GFlag = (int)N + 1;
    ThreadExit(42);
    return 0;
}

int main(void) {
    long Tid;
    int St = 0;

    printf("threadsmoke: pid=%d tid=%d\n", (int)ProcessGetPid(), (int)ThreadGetTid());
    GFlag = 0;
    Tid = ThreadCreate(Worker, (void *)(long)7);
    if (Tid < 0) {
        printf("threadsmoke: create fail %d\n", (int)Tid);
        return 1;
    }
    printf("threadsmoke: created tid=%d\n", (int)Tid);
    if (ThreadJoin(Tid, &St) != 0) {
        printf("threadsmoke: join fail\n");
        return 1;
    }
    printf("threadsmoke: joined st=%d flag=%d\n", St, GFlag);
    if (St != 42 || GFlag != 8) {
        printf("threadsmoke: FAIL\n");
        return 1;
    }
    printf("threadsmoke: ok\n");
    return 0;
}
