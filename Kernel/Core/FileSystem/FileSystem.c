/*
 * FileSystem.c — K9 handoff；K16 内核 Block+FAT 探 BLOCKS.ID
 *
 * 【初学者】
 * 优先内核读盘（virtio-blk + FatProbe）；失败再退回 Boot 的 OsIdSeen。
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "FatProbe.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gHasOsMarker;

int FileSystemHasOsMarker(void) {
    return gHasOsMarker;
}

int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();

    gHasOsMarker = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (HalBlockInit() == 0 && HalBlockReady()) {
        if (FatProbeOsMarker() == 0) {
            gHasOsMarker = 1;
            HalSerialWriteChannel(SLOG_FS, "Fs: BLOCKS.ID ready (kernel)\n");
            return 0;
        }
        HalSerialWriteChannel(SLOG_FS, "Fs: WARN block ok, BLOCKS.ID miss\n");
    } else {
        HalSerialWriteChannel(SLOG_FS, "Fs: WARN no block (virtio-blk)\n");
    }
#endif

    if (Info != 0 && Info->OsIdSeen != 0) {
        gHasOsMarker = 1;
        HalSerialWriteChannel(SLOG_FS, "Fs: BLOCKS.ID ready (Boot handoff)\n");
        return 0;
    }

#if defined(__x86_64__) || defined(_M_X64)
    HalSerialWriteChannel(SLOG_FS, "Fs: WARN no BLOCKS.ID\n");
#else
    HalSerialWriteChannel(SLOG_FS, "Fs: stub (no BLOCKS.ID probe)\n");
#endif
    return 0;
}
