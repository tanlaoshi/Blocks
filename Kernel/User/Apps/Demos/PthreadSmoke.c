/*
 * PthreadSmoke.c — thr-4：ThreadCreate/Join + 自旋 mutex 冒烟
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <Blocks/Process.h>
#include <Blocks/thread.h>

#define LOOPS 2000

static pthread_mutex_t GLock = PTHREAD_MUTEX_INITIALIZER;
static volatile int GCount;

static void *Worker(void *Arg) {
    int N = (int)(long)Arg;
    int i;

    for (i = 0; i < LOOPS; i++) {
        pthread_mutex_lock(&GLock);
        GCount++;
        pthread_mutex_unlock(&GLock);
    }
    printf("pthread: worker tid=%d n=%d\n", (int)ThreadGetTid(), N);
    return (void *)(long)(N + 1);
}

int main(void) {
    long A;
    long B;
    int Sa = 0;
    int Sb = 0;

    printf("pthreadsmoke: pid=%d tid=%d\n", (int)ProcessGetPid(), (int)ThreadGetTid());
    GCount = 0;
    A = ThreadCreate(Worker, (void *)(long)1);
    B = ThreadCreate(Worker, (void *)(long)2);
    if (A < 0 || B < 0) {
        printf("pthreadsmoke: create fail\n");
        return 1;
    }
    if (ThreadJoin(A, &Sa) != 0 || ThreadJoin(B, &Sb) != 0) {
        printf("pthreadsmoke: join fail\n");
        return 1;
    }
    printf("pthreadsmoke: ra=%d rb=%d count=%d expect=%d\n", Sa, Sb, GCount, LOOPS * 2);
    if (Sa != 2 || Sb != 3 || GCount != LOOPS * 2) {
        printf("pthreadsmoke: FAIL\n");
        return 1;
    }
    printf("pthreadsmoke: ok\n");
    return 0;
}
