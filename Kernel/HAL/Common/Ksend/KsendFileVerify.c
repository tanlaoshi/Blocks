/*
 * KsendFileVerify.c — 整文件回读校验与备份恢复
 *
 * 从 KsendFile.c 拆出（行数门禁 ≤300）。读缓冲仍走 KsendFileBuffer /
 * KsendFileReadExact，与扫描/复制同一份 sIoBuffer。
 */
#include "Ksend/Private.h"

int KsendFileVerify(const char *Path, UINT32 Size, const KSEND_IMAGE *Expected,
                    UINT32 ExpectedCrc, UINT32 *OutBlock, UINT32 *OutWant,
                    UINT32 *OutGot) {
    UINT32 Offset = 0;
    UINT32 Crc = KSEND_CRC_INITIAL;
    UINT8 *Buffer = KsendFileBuffer();

    while (Offset < Size) {
        UINT32 Want = Size - Offset;
        UINT32 Got = 0;
        UINT32 Done = 0;
        int Err;

        if (Want > KSEND_IO_CHUNK_BYTES) {
            Want = KSEND_IO_CHUNK_BYTES;
        }
        Err = KsendFileReadExact(Path, Offset, Want, &Got);
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
            BlockCrc = KsendFileBlockCrcOf(Buffer + Done, Take);
            if (Index >= KSEND_MAX_BLOCKS ||
                BlockCrc != Expected->BlockCrc[Index]) {
                *OutBlock = Index;
                *OutWant = Expected->BlockCrc[Index];
                *OutGot = BlockCrc;
                return 0;
            }
            Crc = KsendCrc32Update(Crc, Buffer + Done, Take);
            Done += Take;
        }
        Offset += Got;
    }
    Crc = KsendCrc32Final(Crc);
    if (Crc != ExpectedCrc) {
        /* 块全对、整体不对：尺寸或末块长度有问题 */
        *OutBlock = KSEND_MAX_BLOCKS;
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
