/*
 * SmpInit.c — InitializeSmp（开机模块表；Hal 本层）
 */
#include "KernelModulesPrivate.h"
#include "Hal.h"

int InitializeSmp(void) {
    return HalSmpStartApplicationProcessors();
}
