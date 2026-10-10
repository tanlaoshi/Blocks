/*
 * FatDirectory.c — 根目录列举
 *
 * 【初学者】
 * - 分层：Core/FileSystem
 * - 对外：FatDirectoryListRoot
 * - 不做：子目录递归、LFN 合并
 */
#include "FatFile.h"
#include "FatVolume.h"
#include "Volume.h"

typedef struct {
    FAT_DIR_ENT *Out;
    UINT32 Capacity;
    UINT32 Count;
} LIST_CONTEXT;

/*
 * OnList — WalkRoot 回调：追加一条目录项到 Out
 *
 * 做什么：8.3→显示名、Size、IsDir。
 * 谁调用：仅 FatDirectoryListRoot。
 * 返回：1 表满停止；0 继续
 */
static int OnList(const UINT8 *Entry, void *Context) {
    LIST_CONTEXT *List = (LIST_CONTEXT *)Context;
    if (List->Count >= List->Capacity) {
        return 1;
    }
    FatName83ToDisplay(Entry, List->Out[List->Count].Name);
    List->Out[List->Count].Size = FatRd32(Entry + 28);
    List->Out[List->Count].IsDir = ((Entry[11] & 0x10u) != 0) ? 1 : 0;
    List->Count++;
    return 0;
}

/*
 * FatDirectoryListRoot — 列当前活动卷根目录
 *
 * 做什么：打开活动卷；FatVolumeWalkRoot 填 Out[]。
 * 谁调用：Shell `ls` / Files 窗列表 / Syscall readdir 根。
 * 前后文：前 — VolumeResolve 或默认卷；后 — UI 绘制行。
 * 兄弟 — FatFileWritePath（新建后常刷新列表）。
 * 返回：0 成功；非 0 无卷或 IO 失败
 */
int FatDirectoryListRoot(FAT_DIR_ENT *Out, UINT32 Capacity, UINT32 *Count) {
    FAT_VOLUME Volume;
    LIST_CONTEXT Context;

    if (Out == 0 || Capacity == 0) {
        return -1;
    }
    if (VolumeOpenActive(&Volume) != 0 && FatVolumeOpen(&Volume) != 0) {
        return -1;
    }
    Context.Out = Out;
    Context.Capacity = Capacity;
    Context.Count = 0;
    if (FatVolumeWalkRoot(&Volume, OnList, &Context) != 0) {
        return -1;
    }
    if (Count) {
        *Count = Context.Count;
    }
    return 0;
}
