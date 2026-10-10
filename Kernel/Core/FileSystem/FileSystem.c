/*
 * FileSystem.c — K9 handoff；K16 Block+FAT；K44 FsVol 多卷
 *
 * 【初学者】
 * HalBlockInit → FsVolMountAll（GPT/MBR/superfloppy）→ 默认卷有 BLOCKS/TOYOS 即就绪。
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "FsVol.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gHasOsMarker;

int FileSystemHasOsMarker(void) {
    return gHasOsMarker;
}

int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    const FS_VOL *Def;

    gHasOsMarker = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (HalBlockInit() == 0 && HalBlockReady()) {
        if (FsVolMountAll() == 0) {
            Def = FsVolGet(FsVolDefaultIndex());
            if (Def != 0 && Def->Name[0] == 'B' && Def->Name[1] == 'L') {
                gHasOsMarker = 1;
                HalSerialWriteChannel(SLOG_FS, "Fs: BLOCKS.ID ready (kernel)\n");
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
