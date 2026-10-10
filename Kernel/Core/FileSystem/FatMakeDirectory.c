/*
 * FatMakeDirectory.c — 根目录建子目录
 *
 * 【初学者】
 * - 分层：Core/FileSystem；现网符号曾用 FatMkdir（短名债），此处全词
 * - 对外入口：FatMakeDirectory
 * - 不做：嵌套多层一次建完、rmdir 递归
 */
#include "FatFile.h"
#include "FatPrivate.h"
#include "FatVolume.h"

/*
 * FatMakeDirectory — 在根下建目录（含 . 与 ..）
 *
 * 做什么：占空闲槽；分配簇；写入 . / ..；FAT32 的 .. 指向 RootClus。
 * 谁调用：Shell `mkdir`；Files 窗 New 文件夹（`FatMakeDirectory("NEW")`）。
 * 前后文：
 *   前 — FatDirectorySlotFind（不得已有同名）
 *   后 — FatDirectoryListRoot / Files 刷新
 *   兄弟 — FatFileWritePath / FatDeleteFile
 * 返回：0 成功；非 0 失败（已存在/只读/无槽/IO）
 */
int FatMakeDirectory(const char *Path) {
    FAT_VOLUME Volume;
    char Name83[11];
    FAT_SLOT Slot;
    UINT32 Cluster;
    UINT8 Ent[32];
    UINT8 Dir[FAT_SECTOR];
    UINT32 i;

    if (FatPathResolve83(Path, Name83) != 0 || FatActiveVolumeOpen(&Volume) != 0) {
        return -1;
    }
    if (FatActiveVolumeReadOnly()) {
        return -1;
    }
    if (FatDirectorySlotFind(&Volume, Name83, &Slot) != 0) {
        return -1;
    }
    if (Slot.Have) {
        return -1;
    }
    if (!Slot.Free) {
        return -1;
    }
    Cluster = FatVolumeAllocate(&Volume);
    if (Cluster < 2u) {
        return -1;
    }
    FatMemoryZero(Dir, FAT_SECTOR);
    /* "." */
    for (i = 0; i < 11u; i++) {
        Dir[i] = (UINT8)' ';
    }
    Dir[0] = '.';
    Dir[11] = FAT_ATTR_DIR;
    FatWr16(Dir + 26, (UINT16)(Cluster & 0xFFFFu));
    FatWr16(Dir + 20, (UINT16)((Cluster >> 16) & 0xFFFFu));
    /* ".." → 根 */
    for (i = 0; i < 11u; i++) {
        Dir[32 + i] = (UINT8)' ';
    }
    Dir[32] = '.';
    Dir[33] = '.';
    Dir[32 + 11] = FAT_ATTR_DIR;
    if (Volume.FatBits == 32) {
        FatWr16(Dir + 32 + 26, (UINT16)(Volume.RootClus & 0xFFFFu));
        FatWr16(Dir + 32 + 20, (UINT16)((Volume.RootClus >> 16) & 0xFFFFu));
    }
    if (FatVolumeWriteCluster(&Volume, Cluster, Dir, FAT_SECTOR) != 0) {
        return -1;
    }
    FatDirectoryEntryFill(Ent, Name83, FAT_ATTR_DIR, Cluster, 0);
    return FatDirectoryEntryPut(Slot.FreeLba, Slot.FreeOff, Ent);
}
