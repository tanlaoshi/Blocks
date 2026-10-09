/*
 * FileSystem.c — K9 handoff；K16 内核 Block+FAT 探 TOYOS.ID
 *
 * 【初学者】
 * 优先内核读盘（virtio-blk + FatProbe）；失败再退回 Boot 的 ToyOsIdSeen。
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "FatProbe.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "ToySerialConfig.h"

static int gHasToyOsId;

int FileSystemHasToyOsId(void) {
    return gHasToyOsId;
}

int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gHasToyOsId = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (HalBlockInit() == 0 && HalBlockReady()) {
        if (FatProbeToyOsId() == 0) {
            gHasToyOsId = 1;
            HalSerialWriteChannel(TOY_SLOG_FS, "Fs: TOYOS.ID ready (kernel)\n");
            return 0;
        }
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: WARN block ok, TOYOS.ID miss\n");
    } else {
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: WARN no block (virtio-blk)\n");
    }
#endif

    if (Info != 0 && Info->ToyOsIdSeen != 0) {
        gHasToyOsId = 1;
        HalSerialWriteChannel(TOY_SLOG_FS, "Fs: TOYOS.ID ready (Boot handoff)\n");
        return 0;
    }

#if defined(__x86_64__) || defined(_M_X64)
    HalSerialWriteChannel(TOY_SLOG_FS, "Fs: WARN no TOYOS.ID\n");
#else
    HalSerialWriteChannel(TOY_SLOG_FS, "Fs: stub (no TOYOS.ID probe)\n");
#endif
    return 0;
}
