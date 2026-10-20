/*
 * DriverSched.c — HalDriverSched 槽实现（HAL/Slots，非 Modules 表）
 */
#include "HalDriverSched.h"

static void (*gIoBreath)(void);
static void (*gPreemptDisable)(void);
static void (*gPreemptEnable)(void);

void HalDriverSchedRegister(void (*IoBreath)(void),
                            void (*PreemptDisable)(void),
                            void (*PreemptEnable)(void)) {
    gIoBreath = IoBreath;
    gPreemptDisable = PreemptDisable;
    gPreemptEnable = PreemptEnable;
}

void HalDriverIoBreath(void) {
    if (gIoBreath) {
        gIoBreath();
    }
}

void HalDriverPreemptDisable(void) {
    if (gPreemptDisable) {
        gPreemptDisable();
    }
}

void HalDriverPreemptEnable(void) {
    if (gPreemptEnable) {
        gPreemptEnable();
    }
}
