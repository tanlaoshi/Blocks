/*
 * PlatformStub.c — X64 平台钩子（K10：先存 xHCI 基址）
 *
 * 【初学者】
 * KernelHandoff 会把 Boot 带来的 xHCI 基址、ACPI RSDP、UEFI SystemTable、
 * 以及「高于恒等窗仍须记住」的 Runtime/Loader 范围交给平台层。
 * K10 起 XhciFallback 可读；其余仍空实现。
 */
#include "BootTypes.h"

static UINT64 gXhciFallback;

void HalPlatformSetXhciFallback(UINT64 Address) {
    gXhciFallback = Address;
}

UINT64 HalPlatformXhciFallback(void) {
    return gXhciFallback;
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
