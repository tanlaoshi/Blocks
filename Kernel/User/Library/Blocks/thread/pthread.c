/*
 * pthread.c — thr-4：pthread_* 薄封装 + 自旋 mutex
 */
#include <errno.h>
#include <Host/PortableThread.h>
#include <Blocks/Thread.h>
#include "Atomic.h"

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg) {
    long tid;

    (void)attr;
    if (!thread || !start) {
        errno = EINVAL;
        return EINVAL;
    }
    tid = ThreadCreate(start, arg);
    if (tid < 0) {
        errno = (int)(-tid);
        return errno;
    }
    *thread = (pthread_t)tid;
    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    int st = 0;
    long R;

    R = ThreadJoin((long)thread, &st);
    if (R != 0) {
        errno = (int)(-R);
        return errno;
    }
    if (retval) {
        *retval = (void *)(long)st;
    }
    return 0;
}

void pthread_exit(void *retval) {
    (void)ThreadExit((long)retval);
    for (;;) {
    }
}

int pthread_mutex_init(pthread_mutex_t *M, const pthread_mutexattr_t *attr) {
    (void)attr;
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    M->Lock = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    M->Lock = 0;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    if (BLOCKS_ATOMIC_TAS(&M->Lock, 1) != 0) {
        errno = EAGAIN;
        return EAGAIN;
    }
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    while (BLOCKS_ATOMIC_TAS(&M->Lock, 1) != 0) {
        (void)ThreadYield();
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *M) {
    if (!M) {
        errno = EINVAL;
        return EINVAL;
    }
    BLOCKS_ATOMIC_RELEASE(&M->Lock);
    return 0;
}
