/*
 * KsendFile.c — 卡上文件的整文件 / 分块操作（只用 FileSystem.h 公开 API）
 *
 * 为什么不碰 Fat/Block：铁律要求只调用现有 API；FAT 层没有 truncate，
 * 所以缩小时只能补零（见 KsendFileZeroRange 与 README 的限制一节）。
 *
 * 为什么按 512 KB 读：FileSystemReadFileAt/WriteFileAt 每次都从首簇沿 FAT 链
 * 走到偏移（Fat/FileRead.c SeekFileOffset），4 KB 一块读 2.2 MB 会走第二层
 * O(n²)；512 KB 一块把调用次数压到 1/128（接收路径同样按 512 KB 批量写）。
 *
 * 数据缓冲只有一份 sIoBuffer：Ksend 是同步命令，传输与自检不会并发。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

static UINT8 sIoBuffer[KSEND_IO_CHUNK_BYTES];

KSEND_IMAGE gKsendOldImage;
KSEND_IMAGE gKsendNewImage;

UINT8 *KsendFileBuffer(void) {
    return sIoBuffer;
}

static UINT32 BlockCrcOf(const UINT8 *Data, UINT32 Length) {
    return KsendCrc32Final(KsendCrc32Update(KSEND_CRC_INITIAL, Data, Length));
}

int KsendFileDelete(const char *Path) {
    int Err = FileSystemDeleteFile(Path);

    if (Err == FAT_OK || Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return Err;
}

int KsendFileSize(const char *Path, UINT32 *OutSize) {
    FAT_FILE_STAT Stat;
    int Err = FileSystemFileStat(Path, &Stat);

    if (Err != FAT_OK) {
        return Err;
    }
    *OutSize = Stat.Size;
    return FAT_OK;
}

/* 读满要求才认（短读说明文件比预期短，不能拿残数据去算 CRC） */
static int ReadChunk(const char *Path, UINT32 Offset, UINT32 Want, UINT32 *OutGot) {
    UINTN Got = 0;
    int Err = FileSystemReadFileAt(Path, Offset, sIoBuffer, Want, &Got);

    if (Err != FAT_OK) {
        return Err;
    }
    if (Got != Want) {
        return FAT_ERR_IO;
    }
    *OutGot = (UINT32)Got;
    return FAT_OK;
}

int KsendFileWriteAt(const char *Path, UINT32 Offset, const UINT8 *Data,
                     UINT32 Length) {
    UINTN Done = 0;
    int Err = FileSystemWriteFileAt(Path, Offset, Data, Length, &Done);

    if (Err != FAT_OK) {
        return Err;
    }
    if (Done != Length) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

static int WriteChunk(const char *Path, UINT32 Offset, UINT32 Length) {
    return KsendFileWriteAt(Path, Offset, sIoBuffer, Length);
}

/*
 * 把 sIoBuffer 里 [Offset, Offset+Length) 并进 Image。
 * 调用方保证 Offset 是 4 KB 对齐、Length 是 4 KB 倍数（末块除外），
 * 所以一块不会被两次调用劈开——劈开时块 CRC 会被后一次覆盖而不是续算。
 */
static int ImageAdd(KSEND_IMAGE *Image, UINT32 Offset, UINT32 Length) {
    UINT32 Done = 0;

    if ((Offset % KSEND_BLOCK_BYTES) != 0u) {
        return FAT_ERR_INVAL;
    }
    while (Done < Length) {
        UINT32 Index = (Offset + Done) / KSEND_BLOCK_BYTES;
        UINT32 Take = Length - Done;

        if (Index >= KSEND_MAX_BLOCKS) {
            return FAT_ERR_FILE_TOO_BIG;
        }
        if (Take > KSEND_BLOCK_BYTES) {
            Take = KSEND_BLOCK_BYTES;
        }
        Image->BlockCrc[Index] = BlockCrcOf(sIoBuffer + Done, Take);
        Image->Crc = KsendCrc32Update(Image->Crc, sIoBuffer + Done, Take);
        Done += Take;
    }
    return FAT_OK;
}

static int ImageBegin(KSEND_IMAGE *Image, UINT32 Size) {
    Image->Size = Size;
    Image->BlockCount = (Size + KSEND_BLOCK_BYTES - 1u) / KSEND_BLOCK_BYTES;
    Image->Crc = KSEND_CRC_INITIAL;
    if (Image->BlockCount > KSEND_MAX_BLOCKS) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    return FAT_OK;
}

int KsendFileScan(const char *Path, UINT32 Size, KSEND_IMAGE *Image) {
    UINT32 Offset = 0;
    int Err = ImageBegin(Image, Size);

    if (Err != FAT_OK) {
        return Err;
    }
    while (Offset < Size) {
        UINT32 Want = Size - Offset;
        UINT32 Got = 0;

        if (Want > KSEND_IO_CHUNK_BYTES) {
            Want = KSEND_IO_CHUNK_BYTES;
        }
        Err = ReadChunk(Path, Offset, Want, &Got);
        if (Err != FAT_OK) {
            return Err;
        }
        Err = ImageAdd(Image, Offset, Got);
        if (Err != FAT_OK) {
            return Err;
        }
        Offset += Got;
    }
    Image->Crc = KsendCrc32Final(Image->Crc);
    return FAT_OK;
}

int KsendFileCopy(const char *Source, const char *Target, UINT32 Size,
                  KSEND_IMAGE *Image) {
    UINT32 Offset = 0;
    int Err = ImageBegin(Image, Size);

    if (Err != FAT_OK) {
        return Err;
    }
    /* 目标可能是上次更大的备份：先删，目录项尺寸才等于这次写的字节数 */
    Err = KsendFileDelete(Target);
    if (Err != FAT_OK) {
        return Err;
    }
    while (Offset < Size) {
        UINT32 Want = Size - Offset;
        UINT32 Got = 0;

        if (Want > KSEND_IO_CHUNK_BYTES) {
            Want = KSEND_IO_CHUNK_BYTES;
        }
        Err = ReadChunk(Source, Offset, Want, &Got);
        if (Err != FAT_OK) {
            return Err;
        }
        Err = ImageAdd(Image, Offset, Got);
        if (Err != FAT_OK) {
            return Err;
        }
        Err = WriteChunk(Target, Offset, Got);
        if (Err != FAT_OK) {
            return Err;
        }
        Offset += Got;
    }
    Image->Crc = KsendCrc32Final(Image->Crc);
    return FAT_OK;
}

int KsendFileWriteBlock(const char *Path, UINT32 Index, const UINT8 *Data,
                        UINT32 Length) {
    UINT32 Offset;

    if (Path == 0 || Index >= KSEND_MAX_BLOCKS || Length == 0u ||
        Length > KSEND_BLOCK_BYTES) {
        return FAT_ERR_INVAL;
    }
    Offset = Index * KSEND_BLOCK_BYTES;
    if (Offset + Length > KSEND_MAX_BYTES) {
        return FAT_ERR_FILE_TOO_BIG;
    }
    return KsendFileWriteAt(Path, Offset, Data, Length);
}

/*
 * FAT 没有 truncate：文件缩小时把新末尾之后的旧内容写成 0。
 * 目录项尺寸仍是旧（较大）值，Kernel.elf 会带着一串 0 尾巴——ELF 装载按
 * program header 取，尾巴不参与；但 `ksend info` 看到的是旧尺寸。见 README。
 */
int KsendFileZeroRange(const char *Path, UINT32 Offset, UINT32 Length) {
    UINT32 Done = 0;
    UINT32 i;
    int Err;

    for (i = 0; i < (UINT32)sizeof(sIoBuffer); i++) {
        sIoBuffer[i] = 0;
    }
    while (Done < Length) {
        UINT32 Take = Length - Done;

        if (Take > KSEND_IO_CHUNK_BYTES) {
            Take = KSEND_IO_CHUNK_BYTES;
        }
        Err = WriteChunk(Path, Offset + Done, Take);
        if (Err != FAT_OK) {
            return Err;
        }
        Done += Take;
    }
    return FAT_OK;
}

int KsendFileVerify(const char *Path, UINT32 Size, const KSEND_IMAGE *Expected,
                    UINT32 ExpectedCrc, UINT32 *OutBlock, UINT32 *OutWant,
                    UINT32 *OutGot) {
    UINT32 Offset = 0;
    UINT32 Crc = KSEND_CRC_INITIAL;

    while (Offset < Size) {
        UINT32 Want = Size - Offset;
        UINT32 Got = 0;
        UINT32 Done = 0;
        int Err;

        if (Want > KSEND_IO_CHUNK_BYTES) {
            Want = KSEND_IO_CHUNK_BYTES;
        }
        Err = ReadChunk(Path, Offset, Want, &Got);
        if (Err != FAT_OK) {
            *OutBlock = Offset / KSEND_BLOCK_BYTES;
            *OutWant = 0u;
            *OutGot = (UINT32)Err;
            return 0;
        }
        while (Done < Got) {
            UINT32 Index = (Offset + Done) / KSEND_BLOCK_BYTES;
            UINT32 Take = Got - Done;
            UINT32 BlockCrc;

            if (Take > KSEND_BLOCK_BYTES) {
                Take = KSEND_BLOCK_BYTES;
            }
            BlockCrc = BlockCrcOf(sIoBuffer + Done, Take);
            if (Index >= KSEND_MAX_BLOCKS || BlockCrc != Expected->BlockCrc[Index]) {
                *OutBlock = Index;
                *OutWant = Expected->BlockCrc[Index];
                *OutGot = BlockCrc;
                return 0;
            }
            Crc = KsendCrc32Update(Crc, sIoBuffer + Done, Take);
            Done += Take;
        }
        Offset += Got;
    }
    Crc = KsendCrc32Final(Crc);
    if (Crc != ExpectedCrc) {
        *OutBlock = KSEND_MAX_BLOCKS; /* 块全对、整体不对：尺寸或末块长度有问题 */
        *OutWant = ExpectedCrc;
        *OutGot = Crc;
        return -1;
    }
    return 1;
}

int KsendFileRestore(const char *BackupPath, const char *TargetPath,
                     const KSEND_IMAGE *Expected) {
    KSEND_IMAGE Check;
    UINT32 Block = 0;
    UINT32 Want = 0;
    UINT32 Got = 0;
    int Err;

    if (Expected->Size == 0u) {
        return FAT_ERR_INVAL;
    }
    /* 先删再写：新内核可能更大，留着原目录项尺寸就缩不回去 */
    Err = KsendFileCopy(BackupPath, TargetPath, Expected->Size, &Check);
    if (Err != FAT_OK) {
        return Err;
    }
    if (KsendFileVerify(TargetPath, Expected->Size, Expected, Expected->Crc,
                        &Block, &Want, &Got) != 1) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}
