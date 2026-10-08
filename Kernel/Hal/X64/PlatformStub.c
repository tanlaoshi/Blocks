/*
 * PlatformStub.c — X64 平台钩子桩（完整 HAL 未迁入前）
 *
 * KernelHandoff 会记下 xHCI/RSDP/SystemTable/Runtime 范围；本刀只吞掉调用。
 */
#include "BootTypes.h"

void HalPlatformSetXhciFallback(UINT64 Address) {
    (void)Address;
}

void HalPlatformSetRsdp(UINT64 Address) {
    (void)Address;
}

void HalPlatformSetSystemTable(void *SystemTable) {
    (void)SystemTable;
}

void HalPlatformNoteRuntimeRange(UINT64 Phys, UINT64 Size) {
    (void)Phys;
    (void)Size;
}

void HalSerialInitialize(void) {
}
