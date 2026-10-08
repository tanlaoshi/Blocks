/*
 * FileSystem.c — K9：识盘最小子集
 *
 * 【初学者】
 * 完整路径是 Block → GPT → FAT → 找 TOYOS.ID。
 * 本刀先吃 Boot 在 ExitBootServices 前扫卷的结果（ToyOsIdSeen），
 * 证明「模块表能挂 FileSystem、系统卷可辨认」；AHCI/FAT 另刀迁入。
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

static int gHasToyOsId;

int FileSystemHasToyOsId(void) {
    return gHasToyOsId;
}

int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gHasToyOsId = 0;
    if (Info != 0 && Info->ToyOsIdSeen != 0) {
        gHasToyOsId = 1;
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: TOYOS.ID ready (Boot handoff)\n");
        return 0;
    }

#if defined(__x86_64__) || defined(_M_X64)
    HalSerialWriteChannel(TOY_SLOG_FS, "Fs: WARN no TOYOS.ID (handoff)\n");
#else
    /* Arm/RiscV：本刀无 UEFI 扫卷，软跳过 */
    HalSerialWriteChannel(TOY_SLOG_FS, "Fs: stub (no TOYOS.ID probe)\n");
#endif
    /* 软成功：无系统卷标记也不卡死 Console */
    return 0;
}
