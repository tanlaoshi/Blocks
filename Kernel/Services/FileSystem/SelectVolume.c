/*
 * SelectVolume.c — 按卷选择（PR-FS-vol）
 *
 * 进程级全局状态：FileSystemSelectVolume(Idx) 激活 Idx 并记下它；
 * Operations.c 见 FsVolumeIsSelected() 后放行写（即使挂载策略把 boot/ESP
 * 标成只读）。SelectVolume(-1) 只清选择，不改卷表、不改 ReadOnly 标志。
 *
 * 非线程安全：锁只保护单次调用，不保护「选中 → 读写 → 归还」整段。
 * 只允许 shell/ksend 这类单线程上下文成对使用；重挂卷会重置选择。
 */
#include "FileSystemPrivate.h"

static int gSelectedVol = -1;

int FileSystemSelectVolume(int VolIdx) {
    int Rc;

    FileSystemLock();
    if (VolIdx < 0) {
        gSelectedVol = -1;
        FileSystemUnlock();
        return FAT_OK;
    }
    if (VolIdx >= gVolCount) {
        FileSystemUnlock();
        return FAT_ERR_INVAL;
    }
    Rc = FileSystemActivate(VolIdx);
    if (Rc == FAT_OK) {
        gSelectedVol = VolIdx;
    }
    FileSystemUnlock();
    return Rc;
}

int FsVolumeIsSelected(int Idx) {
    return Idx >= 0 && Idx == gSelectedVol;
}

/* BOOT: 别名：挂载时探到启动固件的那个卷；没有则 -1 */
int FsFindBootVolume(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].BootFw) {
            return i;
        }
    }
    return -1;
}
