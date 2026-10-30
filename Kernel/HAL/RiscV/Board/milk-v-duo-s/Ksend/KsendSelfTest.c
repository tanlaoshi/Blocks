/*
 * KsendSelfTest.c — `ksend selftest`：8 KB 临时文件的写→读回→删
 *
 * 判据（FAT 写在 Duo 上从未验证过，这是所有真传之前必须先跑通的一步）：
 *   1. 新建 KSELFT.BIN（4 KB × 2 块 → 覆盖“建目录项 + 按需扩簇”）；
 *   2. 覆写同名文件（→ 覆盖 FatWriteFileAt 的“既有项”分支）；
 *   3. 每遍都整文件读回、逐字节比对，再算整体 CRC；
 *   4. 删掉临时文件。
 * 图案只依赖偏移，所以读回时能按偏移重算期望值，不必再留一份源数据。
 */
#include "Ksend/Private.h"
#include "FileSystem.h"

/* 可预测图案：同一 Offset 永远同一字节；两遍用不同盐，内容必须不同 */
static UINT8 SelfTestPattern(UINT32 Offset, UINT8 Salt) {
    return (UINT8)((Offset * 31u + (UINT32)Salt) & 0xFFu);
}

static void SelfTestFill(UINT8 *Buffer, UINT32 Offset, UINT32 Length, UINT8 Salt) {
    UINT32 i;

    for (i = 0; i < Length; i++) {
        Buffer[i] = SelfTestPattern(Offset + i, Salt);
    }
}

static int SelfTestCompare(const UINT8 *Buffer, UINT8 Salt, KSEND_SELFTEST *Out) {
    UINT32 Crc = KSEND_CRC_INITIAL;
    UINT32 Offset;

    for (Offset = 0; Offset < KSEND_SELFTEST_BYTES; Offset++) {
        UINT8 Expect = SelfTestPattern(Offset, Salt);

        Crc = KsendCrc32Update(Crc, Buffer + Offset, 1u);
        if (Buffer[Offset] != Expect) {
            Out->FailOffset = Offset;
            Out->FailWant = (UINT32)Expect;
            Out->FailGot = (UINT32)Buffer[Offset];
            return FAT_ERR_IO;
        }
    }
    Out->Crc = KsendCrc32Final(Crc);
    return FAT_OK;
}

/* 一遍：按 4 KB 写满 8 KB，再整读比对 */
static int SelfTestPass(UINT8 Salt, KSEND_SELFTEST *Out) {
    UINT8 *Buffer = KsendFileBuffer();
    UINT32 Offset;
    UINTN Got = 0;
    int Err;

    for (Offset = 0; Offset < KSEND_SELFTEST_BYTES; Offset += KSEND_BLOCK_BYTES) {
        SelfTestFill(Buffer, Offset, KSEND_BLOCK_BYTES, Salt);
        Err = KsendFileWriteAt(KSEND_SELFTEST_PATH, Offset, Buffer,
                               KSEND_BLOCK_BYTES);
        if (Err != FAT_OK) {
            Out->FailOffset = Offset;
            Out->FailGot = (UINT32)Err;
            return Err;
        }
    }
    Err = FileSystemReadFile(KSEND_SELFTEST_PATH, Buffer, KSEND_SELFTEST_BYTES,
                             &Got);
    if (Err != FAT_OK) {
        Out->FailOffset = 0;
        Out->FailGot = (UINT32)Err;
        return Err;
    }
    if (Got != KSEND_SELFTEST_BYTES) {
        Out->FailOffset = 0;
        Out->FailGot = (UINT32)Got;
        return FAT_ERR_IO;
    }
    return SelfTestCompare(Buffer, Salt, Out);
}

int KsendFileSelfTest(KSEND_SELFTEST *Out) {
    int Err;

    Out->Size = KSEND_SELFTEST_BYTES;
    Out->Crc = 0;
    Out->Step = 0;
    Out->FailOffset = KSEND_FAIL_OFFSET_NONE;
    Out->FailWant = 0;
    Out->FailGot = 0;
    Err = KsendFileDelete(KSEND_SELFTEST_PATH);
    if (Err != FAT_OK) {
        Out->FailGot = (UINT32)Err;
        return Err;
    }
    Out->Step = 1;
    Err = SelfTestPass(0u, Out);
    if (Err != FAT_OK) {
        return Err;
    }
    Out->Step = 2;
    Err = SelfTestPass(KSEND_SELFTEST_SALT, Out);
    if (Err != FAT_OK) {
        return Err;
    }
    return KsendFileDelete(KSEND_SELFTEST_PATH);
}
