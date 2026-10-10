/*
 * FileSystem.c — K9 handoff；K16 Block+FAT；K44 Volume 多卷；K46 DataBase
 *
 * 【初学者】
 * HalBlockInit → VolumeMountAll → 默认卷 BLOCKS 就绪 → DataBaseInitialize（BLOCKS.DB）。
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "DataBase.h"
#include "Volume.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gHasOsMarker;

int FileSystemHasOsMarker(void) {
    return gHasOsMarker;
}

int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    const VOLUME *Def;

    gHasOsMarker = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (HalBlockInit() == 0 && HalBlockReady()) {
        if (VolumeMountAll() == 0) {
            Def = VolumeGet(VolumeDefaultIndex());
            if (Def != 0 && Def->Name[0] == 'B' && Def->Name[1] == 'L') {
                gHasOsMarker = 1;
                HalSerialWriteChannel(SLOG_FS, "Fs: BLOCKS.ID ready (kernel)\n");
                (void)DataBaseInitialize();
                return 0;
            }
            HalSerialWriteChannel(SLOG_FS, "Fs: WARN mounted, no BLOCKS\n");
        } else {
            HalSerialWriteChannel(SLOG_FS, "Fs: WARN no FAT volume\n");
        }
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
