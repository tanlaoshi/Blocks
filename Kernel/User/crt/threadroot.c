/*
 * thread_root.c — CRT 入口桩：Start(Arg) 后 ThreadExit
 */
#include <Blocks/thread.h>

void ToyThreadRoot(void *(*Start)(void *), void *Arg) {
    void *Ret = 0;

    if (Start) {
        Ret = Start(Arg);
    }
    (void)ThreadExit((long)Ret);
}
