/*
 * FatDelete.c — 根目录删项
 *
 * 【初学者】
 * - 分层：Core/FileSystem；对标现网 FatFileDelete（薄：只标删除）
 * - 对外入口：FatDeleteFile
 * - 不做：释放簇链（vvfat/现网同策）；递归删目录内容
 */
#include "FatFile.h"
#include "FatPrivate.h"
#include "HalBlock.h"

/*
 * FatDeleteFile — 删除根目录短名项
 *
 * 做什么：找到目录项后首字节改 0xE5；不回收簇。
 * 谁调用：Shell `rm`；Files 窗 Del；DataBase 重建前清旧文件。
 * 前后文：
 *   前 — FatDirectorySlotFind（须 Have）
 *   后 — 列表刷新
 *   兄弟 — FatMakeDirectory / FatRenamePath
 * 返回：0 成功；非 0 失败（不存在/只读/IO）
 */
int FatDeleteFile(const char *Path) {
    FAT_VOLUME Volume;
    char Name83[11];
    FAT_SLOT Slot;
    UINT8 Sec[FAT_SECTOR];

    if (FatPathResolve83(Path, Name83) != 0 || FatActiveVolumeOpen(&Volume) != 0) {
        return -1;
    }
    if (FatActiveVolumeReadOnly()) {
        return -1;
    }
    if (FatDirectorySlotFind(&Volume, Name83, &Slot) != 0 || !Slot.Have) {
        return -1;
    }
    if (HalBlockRead(Slot.EntLba, Sec, 1) != 0) {
        return -1;
    }
    Sec[Slot.EntOff] = 0xE5u;
    return HalBlockWrite(Slot.EntLba, Sec, 1);
}
