/*
 * DriverFile.c — HalDriverFileRead 槽实现（HAL/Slots，非 Modules 表）
 */
#include "HalDriverFile.h"

static HAL_DRIVER_FILE_READ_FN gDriverFileRead;

void HalDriverFileReadRegister(HAL_DRIVER_FILE_READ_FN Fn) {
    gDriverFileRead = Fn;
}

int HalDriverFileRead(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize) {
    if (!gDriverFileRead) {
        if (OutSize) {
            *OutSize = 0;
        }
        return -1;
    }
    return gDriverFileRead(Path, Buffer, MaxSize, OutSize);
}
