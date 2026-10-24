/*
 * pthread.h — 教学自旋 mutex + 薄封装（selfhost：线程主路径用 Thread*）
 * create/join/exit 转 Thread*；非 Linux ABI；勿链宿主 libpthread。
 */
#ifndef PTHREAD_H
#define PTHREAD_H

#include <stddef.h>
#include <Blocks/thread.h>

typedef long pthread_t;
typedef struct {
    int Unused;
} pthread_attr_t;

typedef struct {
    volatile int Lock; /* 0=空闲；1=持有 */
} pthread_mutex_t;

typedef struct {
    int Unused;
} pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

int pthread_create(pthread_t *Thread, const pthread_attr_t *Attr,
                   void *(*Start)(void *), void *Arg);
int pthread_join(pthread_t Thread, void **Retval);
void pthread_exit(void *Retval);

int pthread_mutex_init(pthread_mutex_t *M, const pthread_mutexattr_t *Attr);
int pthread_mutex_destroy(pthread_mutex_t *M);
int pthread_mutex_lock(pthread_mutex_t *M);
int pthread_mutex_trylock(pthread_mutex_t *M);
int pthread_mutex_unlock(pthread_mutex_t *M);

#endif
