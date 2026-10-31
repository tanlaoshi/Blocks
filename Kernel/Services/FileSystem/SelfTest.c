/*
 * SelfTest.c — 跨平台文件系统写自检：BLOCKS:FSELFT.BIN 建/写/读回/覆写/删
 *
 * 判据与 DUO `ksend selftest` 同源（KsendSelfTest.c 已改为调用本文件）：
 * 图案只依赖偏移（Offset*31+盐），读回时按偏移重算期望值，不必留源数据。
 * 盐 0x5A 的覆写遍决定最终 CRC，所以三平台都该得到 87fb2824。
 *
 * 步骤：1 清场 → 2 首遍 8 KB → 3 读回 → 4 覆写+读回 → 5 删+确认。
 * FAT 没有独立 create/open：目录项由第 2 步首次写建立，第 1 步只保证
 * 从「文件不存在」开始（与 ksend 先删 KSELFT.BIN 的做法一致）。
 * 读写全走 FileSystem.h 公开 API，每个调用自带 FS 锁（PR-F5）。
 */
#include "FileSystem.h"
#include "FileSystemPrivate.h"
#include "Console.h"

#define FS_SELFTEST_PATH "BLOCKS:FSELFT.BIN"
/* 8192 / 4096 与 ksend 的 KSEND_SELFTEST_BYTES / KSEND_BLOCK_BYTES 对齐 */
#define FS_SELFTEST_BYTES          8192u
#define FS_SELFTEST_BLOCK_BYTES    4096u
#define FS_SELFTEST_SALT_FIRST     0x00u
#define FS_SELFTEST_SALT_SECOND    0x5Au
#define FS_SELFTEST_PATTERN_FACTOR 31u
#define FS_SELFTEST_BYTE_MASK      0xFFu
/* CRC-32/ISO-HDLC（poly 0xEDB88320，初值/终值异或全 1；与 ksend、zlib.crc32 同参） */
#define FS_SELFTEST_CRC_POLYNOMIAL 0xEDB88320u
#define FS_SELFTEST_CRC_INITIAL    0xFFFFFFFFu
#define FS_SELFTEST_CRC_FINAL_XOR  0xFFFFFFFFu
#define FS_SELFTEST_CRC_BITS       8u
/* UINT32 十进制最多 10 位，留一点余量 */
#define FS_SELFTEST_DECIMAL_MAX    12
/* ksend 的判据是 8 位小写十六进制、不带 0x；Console.WriteHex32 是 0x+大写，不能用 */
#define FS_SELFTEST_HEX_DIGITS     8
#define FS_SELFTEST_HEX_MAX        9
#define FS_SELFTEST_DECIMAL_BASE   10u
#define FS_SELFTEST_HEX_NIBBLE_BITS 4u
#define FS_SELFTEST_HEX_NIBBLE_MASK 0xFu

/* 8 KB 复用缓冲：写入与读回共用，省一份静态数据 */
static UINT8 sBuffer[FS_SELFTEST_BYTES];

static UINT8 PatternByte(UINT32 Offset, UINT8 Salt) {
    return (UINT8)((Offset * FS_SELFTEST_PATTERN_FACTOR + (UINT32)Salt) &
                   FS_SELFTEST_BYTE_MASK);
}

static void FillPattern(UINT8 *Buffer, UINT32 Offset, UINT32 Length, UINT8 Salt) {
    UINT32 i;

    for (i = 0; i < Length; i++) {
        Buffer[i] = PatternByte(Offset + i, Salt);
    }
}

static UINT32 Crc32Update(UINT32 Crc, const UINT8 *Data, UINT32 Length) {
    UINT32 Index;
    UINT32 Bit;

    for (Index = 0; Index < Length; Index++) {
        Crc ^= (UINT32)Data[Index];
        for (Bit = 0; Bit < FS_SELFTEST_CRC_BITS; Bit++) {
            if ((Crc & 1u) != 0u) {
                Crc = (Crc >> 1) ^ FS_SELFTEST_CRC_POLYNOMIAL;
            } else {
                Crc = Crc >> 1;
            }
        }
    }
    return Crc;
}

/* 第 1 步：删旧文件并确认不存在；失败码原样上交 */
static int CreateFresh(void) {
    FAT_FILE_STAT Stat;
    int Err = FileSystemDeleteFile(FS_SELFTEST_PATH);

    if (Err != FAT_OK && Err != FAT_ERR_NOENT) {
        return Err;
    }
    Err = FileSystemFileStat(FS_SELFTEST_PATH, &Stat);
    if (Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return (Err == FAT_OK) ? FAT_ERR_EXIST : Err;
}

/* 第 2/4 步：按 4 KB 写满 8 KB；偏移从 0 开始，已存在就走覆写分支 */
static int WritePass(UINT8 Salt) {
    UINT32 Offset;

    FillPattern(sBuffer, 0, FS_SELFTEST_BYTES, Salt);
    for (Offset = 0; Offset < FS_SELFTEST_BYTES; Offset += FS_SELFTEST_BLOCK_BYTES) {
        UINTN Done = 0;
        int Err = FileSystemWriteFileAt(FS_SELFTEST_PATH, Offset,
                                        sBuffer + Offset, FS_SELFTEST_BLOCK_BYTES,
                                        &Done);
        if (Err != FAT_OK) {
            return Err;
        }
        if (Done != FS_SELFTEST_BLOCK_BYTES) {
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

/* 第 3/4 步：整文件读回（重新打开），逐字节比对，顺带算 CRC */
static int ReadBack(UINT8 Salt, FS_WRITE_SELFTEST *Out) {
    UINTN Got = 0;
    UINT32 Offset;
    int Err = FileSystemReadFile(FS_SELFTEST_PATH, sBuffer, sizeof(sBuffer), &Got);

    if (Err != FAT_OK) {
        return Err;
    }
    if (Got != FS_SELFTEST_BYTES) {
        return FAT_ERR_IO;
    }
    for (Offset = 0; Offset < FS_SELFTEST_BYTES; Offset++) {
        UINT8 Expect = PatternByte(Offset, Salt);

        if (sBuffer[Offset] != Expect) {
            Out->FailOffset = Offset;
            Out->FailWant = (UINT32)Expect;
            Out->FailGot = (UINT32)sBuffer[Offset];
            return FAT_ERR_IO;
        }
    }
    Out->Crc = Crc32Update(FS_SELFTEST_CRC_INITIAL, sBuffer, FS_SELFTEST_BYTES) ^
               FS_SELFTEST_CRC_FINAL_XOR;
    return FAT_OK;
}

/* 第 5 步：删掉并确认真的不存在 */
static int DeleteDone(void) {
    FAT_FILE_STAT Stat;
    int Err = FileSystemDeleteFile(FS_SELFTEST_PATH);

    if (Err != FAT_OK) {
        return Err;
    }
    Err = FileSystemFileStat(FS_SELFTEST_PATH, &Stat);
    if (Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return (Err == FAT_OK) ? FAT_ERR_IO : Err;
}

static void ResultReset(FS_WRITE_SELFTEST *Out) {
    Out->Size = FS_SELFTEST_BYTES;
    Out->Crc = 0;
    Out->Step = 0;
    Out->FailOffset = FS_WRITE_SELFTEST_NO_OFFSET;
    Out->FailWant = 0;
    Out->FailGot = 0;
}

int FileSystemWriteSelfTestRun(FS_WRITE_SELFTEST *Out) {
    FS_WRITE_SELFTEST Local;
    int Err;

    if (!Out) {
        Out = &Local;
    }
    ResultReset(Out);

    Out->Step = FS_WRITE_SELFTEST_STEP_CREATE;
    Err = CreateFresh();
    if (Err != FAT_OK) {
        return Err;
    }
    Out->Step = FS_WRITE_SELFTEST_STEP_WRITE;
    Err = WritePass(FS_SELFTEST_SALT_FIRST);
    if (Err != FAT_OK) {
        return Err;
    }
    Out->Step = FS_WRITE_SELFTEST_STEP_READBACK;
    Err = ReadBack(FS_SELFTEST_SALT_FIRST, Out);
    if (Err != FAT_OK) {
        return Err;
    }
    Out->Step = FS_WRITE_SELFTEST_STEP_OVERWRITE;
    Err = WritePass(FS_SELFTEST_SALT_SECOND);
    if (Err != FAT_OK) {
        return Err;
    }
    Err = ReadBack(FS_SELFTEST_SALT_SECOND, Out);
    if (Err != FAT_OK) {
        return Err;
    }
    Out->Step = FS_WRITE_SELFTEST_STEP_DELETE;
    return DeleteDone();
}

static void PrintUnsigned(UINT32 Value) {
    char Digits[FS_SELFTEST_DECIMAL_MAX];
    int Count = 0;

    if (Value == 0u) {
        gConsole.Write("0");
        return;
    }
    while (Value != 0u && Count < FS_SELFTEST_DECIMAL_MAX) {
        Digits[Count] = (char)('0' + (int)(Value % FS_SELFTEST_DECIMAL_BASE));
        Count++;
        Value /= FS_SELFTEST_DECIMAL_BASE;
    }
    while (Count > 0) {
        Count--;
        gConsole.WriteLen(&Digits[Count], 1);
    }
}

static void PrintHex32(UINT32 Value) {
    char Text[FS_SELFTEST_HEX_MAX];
    int Shift;

    for (Shift = 0; Shift < FS_SELFTEST_HEX_DIGITS; Shift++) {
        UINT32 Nibble = (Value >> ((FS_SELFTEST_HEX_DIGITS - 1 - Shift) *
                                   FS_SELFTEST_HEX_NIBBLE_BITS)) &
                        FS_SELFTEST_HEX_NIBBLE_MASK;

        if (Nibble < FS_SELFTEST_DECIMAL_BASE) {
            Text[Shift] = (char)('0' + (int)Nibble);
        } else {
            Text[Shift] = (char)('a' + (int)(Nibble - FS_SELFTEST_DECIMAL_BASE));
        }
    }
    Text[FS_SELFTEST_HEX_DIGITS] = 0;
    gConsole.Write(Text);
}

static void PrintSigned(int Value) {
    if (Value < 0) {
        gConsole.Write("-");
        PrintUnsigned(0u - (UINT32)Value);
    } else {
        PrintUnsigned((UINT32)Value);
    }
}

static void PrintHeader(void) {
    gConsole.Write("fs selftest: " FS_SELFTEST_PATH " ");
    PrintUnsigned(FS_SELFTEST_BYTES);
    gConsole.Write(" B create + overwrite + readback\n");
}

static void PrintOk(const FS_WRITE_SELFTEST *Result) {
    gConsole.Write("FS SELFTEST OK size=");
    PrintUnsigned(Result->Size);
    gConsole.Write(" crc=");
    PrintHex32(Result->Crc);
    gConsole.Write("\n");
}

static void PrintFail(const FS_WRITE_SELFTEST *Result, int Err) {
    gConsole.Write("FS SELFTEST FAIL step=");
    PrintUnsigned(Result->Step);
    if (Result->FailOffset != FS_WRITE_SELFTEST_NO_OFFSET) {
        gConsole.Write(" at=");
        PrintUnsigned(Result->FailOffset);
        gConsole.Write(" want=");
        PrintHex32(Result->FailWant);
        gConsole.Write(" got=");
        PrintHex32(Result->FailGot);
    } else {
        gConsole.Write(" err=");
        PrintSigned(Err);
    }
    gConsole.Write("\n");
}

int FileSystemWriteSelfTest(void) {
    FS_WRITE_SELFTEST Result;
    int Err;

    PrintHeader();
    Err = FileSystemWriteSelfTestRun(&Result);
    if (Err != FAT_OK) {
        PrintFail(&Result, Err);
        return Err;
    }
    PrintOk(&Result);
    /*
     * 多卷时顺手验「按卷选择 + 写非默认卷」（PR-FS-vol）。单卷/无安全候选
     * 直接返回 0，输出与旧版一致；默认卷那一遍的判据始终在上面。
     */
    return FsWriteSelfTestSecondVolume();
}
