/*
 * Smp.c — InitializeSmp（开机模块编织）
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"

int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}
