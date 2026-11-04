/*
 * sleep.c — sleep / usleep / msleep / clock_ms（全称下划线）
 */
#include <Host/UnixStandard.h>
#include <Blocks/Syscall.h>

unsigned long clock_ms(void) {
    return (unsigned long)blocks_clock_ms();
}

unsigned msleep(unsigned milliseconds) {
    (void)blocks_sleep((long)milliseconds);
    return 0;
}

unsigned sleep(unsigned seconds) {
    unsigned long milliseconds;

    if (seconds == 0) {
        return 0;
    }
    if (seconds > 60u) {
        seconds = 60u;
    }
    milliseconds = (unsigned long)seconds * 1000ul;
    (void)blocks_sleep((long)milliseconds);
    return 0;
}

int usleep(unsigned microseconds) {
    unsigned milliseconds;

    if (microseconds == 0) {
        return 0;
    }
    milliseconds = (microseconds + 999u) / 1000u;
    if (milliseconds == 0) {
        milliseconds = 1;
    }
    (void)blocks_sleep((long)milliseconds);
    return 0;
}
