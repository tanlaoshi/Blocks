/*
 * sleep.c — sleep / usleep / msleep / clock_ms（SYS_TIME_SLEEP / SYS_CLOCK_MS）
 */
#include <unistd.h>
#include <Blocks/syscall.h>

unsigned long clock_ms(void) {
    return (unsigned long)blocks_clock_ms();
}

unsigned msleep(unsigned ms) {
    (void)blocks_sleep((long)ms);
    return 0;
}

unsigned sleep(unsigned seconds) {
    unsigned long Ms;

    if (seconds == 0) {
        return 0;
    }
    if (seconds > 60u) {
        seconds = 60u;
    }
    Ms = (unsigned long)seconds * 1000ul;
    (void)blocks_sleep((long)Ms);
    return 0;
}

int usleep(unsigned usec) {
    unsigned Ms;

    if (usec == 0) {
        return 0;
    }
    Ms = (usec + 999u) / 1000u;
    if (Ms == 0) {
        Ms = 1;
    }
    (void)blocks_sleep((long)Ms);
    return 0;
}
