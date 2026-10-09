#include "HalSyscall.h"

int HalSyscallInit(void) {
    return -1;
}

int HalSyscallRun(UINT64 Entry, UINT64 StackTop) {
    (void)Entry;
    (void)StackTop;
    return -1;
}
