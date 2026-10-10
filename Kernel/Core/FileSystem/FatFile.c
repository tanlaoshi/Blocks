/*
 * FatFile.c — 按 8.3 / 路径读根目录文件
 *
 * 【初学者】
 * - 分层：Core/FileSystem；写路径见 FatWrite.c
 * - 对外：FatFileReadPath / FatFileRead83
 * - 不做：子目录、追加写、LFN
 */
#include "FatFile.h"
#include "FatVolume.h"
#include "Volume.h"
#include "HalBlock.h"

#define MAX_FILE (8u * 1024u * 1024u)

typedef struct {
    const char *Want83;
    UINT32 Cluster;
    UINT32 Size;
    int Found;
} FIND_CONTEXT;

/*
 * MemoryEqual — 比较目录项 11 字节名
 *
 * 做什么：逐字节比 N 长。
 * 谁调用：仅 OnFind。
 * 返回：1 相等；0 不等
 */
static int MemoryEqual(const UINT8 *A, const char *B, UINTN N) {
    UINTN Index;
    for (Index = 0; Index < N; Index++) {
        if (A[Index] != (UINT8)B[Index]) {
            return 0;
        }
    }
    return 1;
}

/*
 * OnFind — FatVolumeWalkRoot 回调：匹配 Want83
 *
 * 做什么：命中则填 Cluster/Size 并请求停止扫描。
 * 谁调用：仅 FatFileRead83。
 * 返回：1 停止；0 继续
 */
static int OnFind(const UINT8 *Entry, void *Context) {
    FIND_CONTEXT *Find = (FIND_CONTEXT *)Context;
    if (MemoryEqual(Entry, Find->Want83, 11)) {
        Find->Cluster = ((UINT32)FatRd16(Entry + 20) << 16) | FatRd16(Entry + 26);
        Find->Size = FatRd32(Entry + 28);
        Find->Found = 1;
        return 1;
    }
    return 0;
}

/*
 * FatFileRead83 — 按 11 字节短名读整文件
 *
 * 做什么：根目录找项；沿 FAT 链读至 Size；上限 Capacity / MAX_FILE。
 * 谁调用：FatFileReadPath；Theme/Font 等直接 8.3 读。
 * 前后文：前 — VolumeOpenActive；后 — 调用方解析内容。
 * 兄弟 — FatFileWritePath（写对称）。
 * 返回：字节数 ≥0；失败 -1
 */
int FatFileRead83(const char Name83[11], void *Buffer, UINT32 Capacity, UINT32 *OutSize) {
    FAT_VOLUME Volume;
    FIND_CONTEXT Find;
    UINT8 Sector[FAT_SECTOR];
    UINT8 *Destination = (UINT8 *)Buffer;
    UINT32 Got = 0;
    UINT32 Guard;
    UINT32 Index;

    if (Buffer == 0 || Capacity == 0 || Name83 == 0) {
        return -1;
    }
    if (VolumeOpenActive(&Volume) != 0 && FatVolumeOpen(&Volume) != 0) {
        return -1;
    }
    Find.Want83 = Name83;
    Find.Cluster = 0;
    Find.Size = 0;
    Find.Found = 0;
    if (FatVolumeWalkRoot(&Volume, OnFind, &Find) != 0 || !Find.Found ||
        Find.Cluster < 2u || Find.Size == 0) {
        return -1;
    }
    if (Find.Size > Capacity || Find.Size > MAX_FILE) {
        return -1;
    }
    {
        UINT32 Cluster = Find.Cluster;
        for (Guard = 0; Guard < 16384u && Cluster >= 2u && Got < Find.Size; Guard++) {
            UINT32 LogicalBlock = Volume.DataLba + (Cluster - 2u) * (UINT32)Volume.Spc;
            UINT8 SectorInCluster;
            for (SectorInCluster = 0; SectorInCluster < Volume.Spc && Got < Find.Size;
                 SectorInCluster++) {
                UINT32 Chunk = Find.Size - Got;
                if (Chunk > FAT_SECTOR) {
                    Chunk = FAT_SECTOR;
                }
                if (HalBlockRead(LogicalBlock + SectorInCluster, Sector, 1) != 0) {
                    return -1;
                }
                for (Index = 0; Index < Chunk; Index++) {
                    Destination[Got + Index] = Sector[Index];
                }
                Got += Chunk;
            }
            Cluster = FatVolumeNext(&Volume, Cluster);
            if (Volume.FatBits == 32) {
                if (Cluster < 2u || Cluster >= 0x0FFFFFF8u) {
                    break;
                }
            } else if (Cluster < 2u || Cluster >= 0xFFF8u) {
                break;
            }
        }
    }
    if (Got < Find.Size) {
        return -1;
    }
    if (OutSize) {
        *OutSize = Find.Size;
    }
    return (int)Find.Size;
}

/*
 * FatFileReadPath — 带可选 VOL: 前缀的路径读
 *
 * 做什么：VolumeResolve → FatPathTo83 → FatFileRead83。
 * 谁调用：DataBaseLoad / StoreLoadCatalog / Shell `cat` / Syscall 读。
 * 前后文：前 — VolumeMountAll；兄弟 — FatFileWritePath。
 * 返回：同 FatFileRead83
 */
int FatFileReadPath(const char *Path, void *Buffer, UINT32 Capacity, UINT32 *OutSize) {
    char Name83[11];
    const char *Relative = Path;

    if (Path == 0) {
        return -1;
    }
    (void)VolumeResolve(Path, &Relative);
    if (Relative == 0 || Relative[0] == 0) {
        return -1;
    }
    if (FatPathTo83(Relative, Name83) != 0) {
        return -1;
    }
    return FatFileRead83(Name83, Buffer, Capacity, OutSize);
}
