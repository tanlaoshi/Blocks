/*
 * HandoffPlatform.c — 交接包里的平台指针写入 HalPlatform
 */
#include "HandoffPrivate.h"

extern void HalPlatformSetXhciFallback(UINT64 Address);
extern void HalPlatformSetRsdp(UINT64 Address);
extern void HalPlatformSetSystemTable(void *SystemTable);

void HandoffFillPlatform(const UEFI_BOOT_CONFIG *Cfg) {
    HalPlatformSetXhciFallback(Cfg->XhciBaseAddress);
    HalPlatformSetRsdp(Cfg->RsdpAddress);
    HalPlatformSetSystemTable(Cfg->SystemTable);
}
