/*
 * FatRename.c — 同卷根目录改名
 *
 * 【初学者】
 * - 分层：Core/FileSystem；对标现网 FatFileDelete 内改名路径
 * - 对外入口：FatRenamePath
 * - 不做：跨卷、搬簇、覆盖已存在新名
 */
#include "FatFile.h"
#include "FatPrivate.h"
#include "Volume.h"

/*
 * FatRenamePath — 同卷改名（根路径，可带 VOL:）
 *
 * 做什么：校验两路径同卷；改目录项 8.3 名区；数据簇不动。
 * 谁调用：Shell `mv`；将来 Files 窗改名若做也走这里。
 * 前后文：
 *   前 — FatMakeDirectory / FatFileWritePath（建出后再改名）
 *   后 — FatDirectoryListRoot / Files 刷新
 *   兄弟 — FatDeleteFile；FatPathTo83（本函数内）
 * 返回：0 成功（含新旧名相同）；非 0 失败
 */
int FatRenamePath(const char *OldPath, const char *NewPath) {
    FAT_VOLUME Volume;
    char Old83[11];
    char New83[11];
    const char *RelativeOld;
    const char *RelativeNew;
    int VolumeIndex;
    FAT_SLOT SlotOld;
    FAT_SLOT SlotNew;
    UINT8 Ent[32];
    UINT32 i;
    int Same;

    if (OldPath == 0 || NewPath == 0) {
        return -1;
    }
    if (VolumeResolve(OldPath, &RelativeOld) != 0 || RelativeOld == 0 ||
        RelativeOld[0] == 0) {
        return -1;
    }
    VolumeIndex = VolumeActiveIndex();
    if (FatPathTo83(RelativeOld, Old83) != 0) {
        return -1;
    }
    if (VolumeResolve(NewPath, &RelativeNew) != 0 || RelativeNew == 0 ||
        RelativeNew[0] == 0) {
        return -1;
    }
    if (VolumeActiveIndex() != VolumeIndex) {
        return -1; /* 跨卷不做 */
    }
    if (FatPathTo83(RelativeNew, New83) != 0) {
        return -1;
    }
    Same = 1;
    for (i = 0; i < 11u; i++) {
        if (Old83[i] != New83[i]) {
            Same = 0;
            break;
        }
    }
    if (Same) {
        return 0;
    }
    if (FatActiveVolumeOpen(&Volume) != 0 || FatActiveVolumeReadOnly()) {
        return -1;
    }
    if (FatDirectorySlotFind(&Volume, Old83, &SlotOld) != 0 || !SlotOld.Have) {
        return -1;
    }
    if (FatDirectorySlotFind(&Volume, New83, &SlotNew) != 0) {
        return -1;
    }
    if (SlotNew.Have) {
        return -1;
    }
    for (i = 0; i < 32u; i++) {
        Ent[i] = SlotOld.Ent[i];
    }
    for (i = 0; i < 11u; i++) {
        Ent[i] = (UINT8)New83[i];
    }
    return FatDirectoryEntryPut(SlotOld.EntLba, SlotOld.EntOff, Ent);
}
