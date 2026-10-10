/*
 * FileSystem.c — 块设备 + 多卷 + BLOCKS.DB 总入口
 *
 * 【初学者】
 * - 分层：Core/FileSystem 主文件（保留目录名）
 * - 对外：FileSystemInitialize / FileSystemHasOsMarker
 * - 边界：不实现具体 FAT 读写（见 FatFile / Volume）
 */
#include "FileSystem.h"
#include "BootInfo.h"
#include "DataBase.h"
#include "Volume.h"
#include "HalBlock.h"
#include "HalSerial.h"
#include "SerialConfig.h"

static int gHasOsMarker;

/*
 * FileSystemHasOsMarker — 是否已确认 BLOCKS.ID / Boot handoff
 *
 * 做什么：返回 gHasOsMarker（Volume 或 Boot 传入）。
 * 谁调用：Gui/Desktop 决定是否显示「无系统盘」类提示。
 * 返回：1 有标记；0 无
 */
int FileSystemHasOsMarker(void) {
    return gHasOsMarker;
}

/*
 * FileSystemInitialize — 启动时装块设备、挂卷、开库
 *
 * 做什么：x64 上 HalBlockInitialize → VolumeMountAll → 默认 BLOCKS 卷 → DataBaseInitialize；
 *         无块设备时看 BootInfo OsIdSeen。
 * 谁调用：KernelMain / 早期 Core 初始化。
 * 前后文：前 — HalBlock 驱动；后 — Shell / Theme 读 BLOCKS.DB。
 * 返回：0（警告路径也返回 0，日志见串口）
 */
int FileSystemInitialize(void) {
    const BOOT_INFO *Info = BootInfoGet();
    const VOLUME *DefaultVolume;

    gHasOsMarker = 0;

#if defined(__x86_64__) || defined(_M_X64)
    if (HalBlockInitialize() == 0 && HalBlockReady()) {
        if (VolumeMountAll() == 0) {
            DefaultVolume = VolumeGet(VolumeDefaultIndex());
            if (DefaultVolume != 0 && DefaultVolume->Name[0] == 'B' &&
                DefaultVolume->Name[1] == 'L') {
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
