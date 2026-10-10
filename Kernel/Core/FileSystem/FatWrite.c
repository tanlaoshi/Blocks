/*
 * FatWrite.c — 根目录整文件写
 *
 * 【初学者】
 * - 分层：Core/FileSystem；对标现网 FatFileWrite*（实现保持 Blocks 已测路径）
 * - 对外入口：FatFileWritePath
 * - 不做：追加写、子目录、>WRITE_MAX、跨卷
 */
#include "FatFile.h"
#include "FatPrivate.h"
#include "FatVolume.h"

#define WRITE_MAX 4096u

/*
 * FatFileWritePath — 按路径写整文件到根目录
 *
 * 做什么：解析 8.3；已存在则覆写簇与目录项；否则占空闲槽新建。
 *         空内容仍写 1 字节（vvfat 宿主侧才看得见）。
 * 谁调用：Shell `write`；Theme/DataBase/Store 等落盘；Files 窗新建文本若走写接口。
 * 前后文：
 *   前 — Volume 已挂载；FatPathResolve83 / FatDirectorySlotFind
 *   后 — FatDirectoryListRoot / 调用方刷新
 *   兄弟 — FatMakeDirectory / FatDeleteFile / FatRenamePath
 * 返回：0 成功；非 0 失败（只读/满目录/IO/参数）
 */
int FatFileWritePath(const char *Path, const void *Buffer, UINT32 Length) {
    FAT_VOLUME Volume;
    char Name83[11];
    FAT_SLOT Slot;
    UINT32 Cluster;
    UINT32 StoreLen = Length;
    const UINT8 *Source = (const UINT8 *)Buffer;
    static const UINT8 Pad[1] = { 0 };
    UINT8 Ent[32];

    if (Path == 0 || (Buffer == 0 && Length > 0) || Length > WRITE_MAX) {
        return -1;
    }
    if (FatPathResolve83(Path, Name83) != 0 || FatActiveVolumeOpen(&Volume) != 0) {
        return -1;
    }
    if (FatActiveVolumeReadOnly()) {
        return -1;
    }
    /* vvfat：空文件仍写 1 字节，否则宿主侧常不现 */
    if (StoreLen == 0) {
        Source = Pad;
        StoreLen = 1;
    }
    if (FatDirectorySlotFind(&Volume, Name83, &Slot) != 0) {
        return -1;
    }
    if (Slot.Have) {
        if ((Slot.Ent[11] & FAT_ATTR_DIR) != 0) {
            return -1;
        }
        Cluster = ((UINT32)FatRd16(Slot.Ent + 20) << 16) | FatRd16(Slot.Ent + 26);
        if (Cluster < 2u) {
            Cluster = FatVolumeAllocate(&Volume);
            if (Cluster < 2u) {
                return -1;
            }
        }
        if (FatVolumeWriteCluster(&Volume, Cluster, Source, StoreLen) != 0) {
            return -1;
        }
        FatDirectoryEntryFill(Ent, Name83, FAT_ATTR_ARCH, Cluster, StoreLen);
        return FatDirectoryEntryPut(Slot.EntLba, Slot.EntOff, Ent);
    }
    if (!Slot.Free) {
        return -1;
    }
    Cluster = FatVolumeAllocate(&Volume);
    if (Cluster < 2u) {
        return -1;
    }
    if (FatVolumeWriteCluster(&Volume, Cluster, Source, StoreLen) != 0) {
        return -1;
    }
    FatDirectoryEntryFill(Ent, Name83, FAT_ATTR_ARCH, Cluster, StoreLen);
    return FatDirectoryEntryPut(Slot.FreeLba, Slot.FreeOff, Ent);
}
