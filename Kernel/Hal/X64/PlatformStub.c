/*
 * PlatformStub.c — X64 平台钩子（当前为空实现）
 *
 * 【初学者】
 * KernelHandoff 会把 Boot 带来的 xHCI 基址、ACPI RSDP、UEFI SystemTable、
 * 以及「高于恒等窗仍须记住」的 Runtime/Loader 范围交给平台层。
 * 当前先吞掉调用；USB / 时钟 / 运行时服务接上后再填充存储逻辑。
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
