/*
 * KsendCache.c — 目标文件旁的「差量表」缓存（<目标全名>.crc）
 *
 * 人话：把上次成功写入后每 4 KB 一块的 CRC 留在卡上，下次 READY 之前直接
 * 读这份 2 KB 表，不再把 2 MB 的目标文件整个读一遍（SDIO 512 B 一块，
 * 整读一次要几千次往返）。缓存只是加速；删掉它随时退回慢路径。
 * 从哪读：KsendCacheLoad（PrepareBackup 的 --no-backup 分支）
 *         → KsendCacheSave（整文件读回校验通过之后）。
 * 别改：字段布局/大小（Tools/ksend.py 与 README 同步描述）：
 *   u32 magic=0x4B534352, version=1, block_size=4096, block_count,
 *   total_size, full_crc，随后 block_crc[block_count]（全部小端 u32）。
 *
 * 安全边界（重要）：
 *   - 能从 O(1) 的 stat 确认的只有 total_size（= 目标当前大小）；full_crc
 *     不读文件就无法确认，只能信任「这份 .crc 是上次校验通过后自己写的」。
 *   - 所以外部写入（读卡器刷盘、别的命令改文件）会让缓存过期。刷新卡的
 *     脚本必须删掉 .crc；板端每次传完仍会整文件读回校验，过期缓存只会
 *     让这次部署 FAIL（然后回滚/重试），不会在「校验通过」的前提下写错。
 *   - 文件变小时 FAT 没有 truncate，目标目录项尺寸仍是旧值：这时缓存的
 *     total_size 对不上，KsendTransfer 会删掉旧缓存，下次走慢路径。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static char sCachePath[KSEND_PATH_BYTES];

static UINT32 CacheRead32(const UINT8 *Data) {
    return (UINT32)Data[0] | ((UINT32)Data[1] << 8) |
           ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

static void CacheWrite32(UINT8 *Data, UINT32 Value) {
    Data[0] = (UINT8)(Value & 0xFFu);
    Data[1] = (UINT8)((Value >> 8) & 0xFFu);
    Data[2] = (UINT8)((Value >> 16) & 0xFFu);
    Data[3] = (UINT8)((Value >> 24) & 0xFFu);
}

/* 目标全名 + ".crc"：同卷同目录，保留原大小写（与 .bak 的命名规则一致） */
static int CachePathBuild(const char *Target) {
    const char *Walk;
    UINT32 Index = 0;

    if (Target == 0 || Target[0] == 0) {
        return FAT_ERR_INVAL;
    }
    while (Target[Index] != 0) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        sCachePath[Index] = Target[Index];
        Index++;
    }
    for (Walk = KSEND_CACHE_EXTENSION; *Walk != 0; Walk++) {
        if (Index + 1u >= KSEND_PATH_BYTES) {
            return FAT_ERR_INVAL;
        }
        sCachePath[Index++] = *Walk;
    }
    sCachePath[Index] = 0;
    return FAT_OK;
}

/* 读满才认：短读说明缓存被截断，不能拿残表当旧内容 */
static int CacheReadExact(const char *Path, UINT32 Offset, UINT32 Length,
                          UINT8 *Data) {
    UINTN Got = 0;
    int Err = FileSystemReadFileAt(Path, Offset, Data, Length, &Got);

    if (Err != FAT_OK) {
        return Err;
    }
    if (Got != (UINTN)Length) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

/* 1=头部字段自洽且与目标当前大小一致；0=缓存不可用（退回慢路径） */
static int CacheHeaderCheck(const UINT8 *Header, UINT32 CacheSize,
                            UINT32 TargetSize) {
    UINT32 BlockCount;
    UINT32 TotalSize;

    if (CacheRead32(Header + KSEND_CACHE_OFFSET_MAGIC) != KSEND_CACHE_MAGIC ||
        CacheRead32(Header + KSEND_CACHE_OFFSET_VERSION) != KSEND_CACHE_VERSION ||
        CacheRead32(Header + KSEND_CACHE_OFFSET_BLOCK_BYTES) !=
            KSEND_BLOCK_BYTES) {
        return 0;
    }
    BlockCount = CacheRead32(Header + KSEND_CACHE_OFFSET_BLOCK_COUNT);
    TotalSize = CacheRead32(Header + KSEND_CACHE_OFFSET_TOTAL_SIZE);
    if (TotalSize != TargetSize || TotalSize == 0u ||
        TotalSize > KSEND_MAX_BYTES) {
        return 0;
    }
    if (BlockCount !=
        (TotalSize + KSEND_BLOCK_BYTES - 1u) / KSEND_BLOCK_BYTES) {
        return 0;
    }
    if (BlockCount == 0u || BlockCount > KSEND_MAX_BLOCKS) {
        return 0;
    }
    if (CacheSize != KSEND_CACHE_HEADER_BYTES + BlockCount * KSEND_WORD_BYTES) {
        return 0;
    }
    return 1;
}

/* 把已校验的块表搬进 Image；表外槽位清零，避免用到上一趟的残留 */
static void CacheFillImage(const UINT8 *Table, UINT32 BlockCount,
                           UINT32 TotalSize, UINT32 FullCrc, KSEND_IMAGE *Image) {
    UINT32 Index;

    Image->Size = TotalSize;
    Image->BlockCount = BlockCount;
    Image->Crc = FullCrc;
    for (Index = 0; Index < BlockCount; Index++) {
        Image->BlockCrc[Index] =
            CacheRead32(Table + Index * KSEND_WORD_BYTES);
    }
    for (; Index < KSEND_MAX_BLOCKS; Index++) {
        Image->BlockCrc[Index] = 0;
    }
}

/*
 * 加载 <目标>.crc。命中返回 FAT_OK 且 Image 完整可用；否则返回错误码，
 * 调用方必须退回逐块读文件的慢路径（缓存是加速，不是事实来源）。
 */
int KsendCacheLoad(const char *TargetPath, UINT32 TargetSize,
                   KSEND_IMAGE *Image) {
    UINT8 *Buffer = KsendFileBuffer();
    UINT32 CacheSize = 0;
    UINT32 BlockCount;
    UINT32 TotalSize;
    UINT32 FullCrc;
    int Err;

    if (Image == 0 || TargetSize == 0u) {
        return FAT_ERR_INVAL;
    }
    Err = CachePathBuild(TargetPath);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = KsendFileSize(sCachePath, &CacheSize);
    if (Err != FAT_OK) {
        return Err;
    }
    if (CacheSize < KSEND_CACHE_HEADER_BYTES ||
        CacheSize > KSEND_CACHE_MAX_BYTES) {
        return FAT_ERR_INVAL;
    }
    Err = CacheReadExact(sCachePath, 0, KSEND_CACHE_HEADER_BYTES, Buffer);
    if (Err != FAT_OK) {
        return Err;
    }
    if (!CacheHeaderCheck(Buffer, CacheSize, TargetSize)) {
        return FAT_ERR_INVAL;
    }
    BlockCount = CacheRead32(Buffer + KSEND_CACHE_OFFSET_BLOCK_COUNT);
    TotalSize = CacheRead32(Buffer + KSEND_CACHE_OFFSET_TOTAL_SIZE);
    FullCrc = CacheRead32(Buffer + KSEND_CACHE_OFFSET_FULL_CRC);
    Err = CacheReadExact(sCachePath, KSEND_CACHE_HEADER_BYTES,
                         BlockCount * KSEND_WORD_BYTES, Buffer);
    if (Err != FAT_OK) {
        return Err;
    }
    CacheFillImage(Buffer, BlockCount, TotalSize, FullCrc, Image);
    return FAT_OK;
}

/*
 * 让 <目标>.crc 失效（删掉）。失败/回滚/缩小时用：与其猜旧表是否还
 * 与磁盘一致，不如让下一次重新慢扫——缓存只是加速，慢路径永远正确。
 */
int KsendCacheDrop(const char *TargetPath) {
    int Err = CachePathBuild(TargetPath);

    if (Err != FAT_OK) {
        return Err;
    }
    return KsendFileDelete(sCachePath);
}

/*
 * 写 <目标>.crc（先删再写：FAT 没有 truncate，旧缓存更长会留下尾巴）。
 * 调用前提：目标已整文件读回校验通过，Image 就是目标此刻的准确分块图。
 * 返回错误只影响下次快慢，不影响本次传输是否成功。
 */
int KsendCacheSave(const char *TargetPath, const KSEND_IMAGE *Image) {
    UINT8 *Buffer = KsendFileBuffer();
    UINT32 BlockCount;
    UINT32 Bytes;
    UINT32 Index;
    int Err;

    if (TargetPath == 0 || Image == 0) {
        return FAT_ERR_INVAL;
    }
    BlockCount = Image->BlockCount;
    if (BlockCount == 0u || BlockCount > KSEND_MAX_BLOCKS ||
        BlockCount !=
            (Image->Size + KSEND_BLOCK_BYTES - 1u) / KSEND_BLOCK_BYTES) {
        return FAT_ERR_INVAL;
    }
    Bytes = KSEND_CACHE_HEADER_BYTES + BlockCount * KSEND_WORD_BYTES;
    if (Bytes > KSEND_CACHE_MAX_BYTES) {
        return FAT_ERR_INVAL;
    }
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_MAGIC, KSEND_CACHE_MAGIC);
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_VERSION, KSEND_CACHE_VERSION);
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_BLOCK_BYTES, KSEND_BLOCK_BYTES);
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_BLOCK_COUNT, BlockCount);
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_TOTAL_SIZE, Image->Size);
    CacheWrite32(Buffer + KSEND_CACHE_OFFSET_FULL_CRC, Image->Crc);
    for (Index = 0; Index < BlockCount; Index++) {
        CacheWrite32(Buffer + KSEND_CACHE_HEADER_BYTES +
                         Index * KSEND_WORD_BYTES,
                     Image->BlockCrc[Index]);
    }
    Err = CachePathBuild(TargetPath);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = KsendFileDelete(sCachePath);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = KsendFileWriteAt(sCachePath, 0, Buffer, Bytes);
    if (Err != FAT_OK) {
        return Err;
    }
    return FileSystemFileSync(sCachePath);
}
