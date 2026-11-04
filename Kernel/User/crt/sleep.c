/*
 * sleep.c — sleep / usleep / msleep / clock_ms（展览级命名）
 */
#include <unistd.h>
#include <Blocks/syscall.h>

unsigned long clock_ms(void) {
    return (unsigned long)blocks_clock_ms();
}

unsigned msleep(unsigned Milliseconds) {
    (void)blocks_sleep((long)Milliseconds);
    return 0;
}

unsigned sleep(unsigned Seconds) {
    unsigned long Milliseconds;

    if (Seconds == 0) {
        return 0;
    }
    if (Seconds > 60u) {
        Seconds = 60u;
    }
    Milliseconds = (unsigned long)Seconds * 1000ul;
    (void)blocks_sleep((long)Milliseconds);
    return 0;
}

int usleep(unsigned Microseconds) {
    unsigned Milliseconds;

    if (Microseconds == 0) {
        return 0;
    }
    Milliseconds = (Microseconds + 999u) / 1000u;
    if (Milliseconds == 0) {
        Milliseconds = 1;
    }
    (void)blocks_sleep((long)Milliseconds);
    return 0;
}
