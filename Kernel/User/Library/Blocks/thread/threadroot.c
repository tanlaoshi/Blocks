/*
 * thread_root.c — CRT 入口桩：Start(Arg) 后 ThreadExit
 */
#include <Blocks/Thread.h>

void ToyThreadRoot(void *(*start)(void *), void *arg) {
    void *ret = 0;

    if (start) {
        ret = start(arg);
    }
    (void)ThreadExit((long)ret);
}
