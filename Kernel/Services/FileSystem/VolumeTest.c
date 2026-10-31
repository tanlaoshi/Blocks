/*
 * VolumeTest.c — `fs selftest` 的第二卷写路径（PR-FS-vol）
 *
 * 只由 FileSystemWriteSelfTest() 末尾调用；`ksend selftest` 走
 * FileSystemWriteSelfTestRun()，不触发本文件，板端判据不变。
 *
 * 选哪卷：非默认、带 BLOCKS.ID、且挂载时没探到启动固件（BootFw=0）的 FAT。
 * 刻意不碰 boot 分区：哪怕它被策略标成只读，FileSystemSelectVolume() 也能
 * 放行写入，自检不该拿启动盘冒险。选中 → 建 4 KB → 写 → 读回比对 → 删；
 * 无论成败都 SelectVolume(-1) 归还，不把全局选择留给下一个调用方。
 */
#include "FileSystemPrivate.h"
#include "Console.h"

#define FS_VOLTEST_PATH_BYTES      16u
#define FS_VOLTEST_BYTES           4096u
#define FS_VOLTEST_PATTERN_FACTOR  31u
#define FS_VOLTEST_SALT            0x5Au
#define FS_VOLTEST_BYTE_MASK       0xFFu
#define FS_VOLTEST_DECIMAL_MAX     12
#define FS_VOLTEST_DECIMAL_BASE    10u

/* 4 KB 复用缓冲：写与读回共用 */
static UINT8 sVolTestBuffer[FS_VOLTEST_BYTES];

static UINT8 PatternByte(UINT32 Offset) {
    return (UINT8)((Offset * FS_VOLTEST_PATTERN_FACTOR + FS_VOLTEST_SALT) &
                   FS_VOLTEST_BYTE_MASK);
}

static void PrintUnsigned(UINT32 Value) {
    char Digits[FS_VOLTEST_DECIMAL_MAX];
    int Count = 0;

    if (Value == 0u) {
        gConsole.Write("0");
        return;
    }
    while (Value != 0u && Count < FS_VOLTEST_DECIMAL_MAX) {
        Digits[Count] = (char)('0' + (int)(Value % FS_VOLTEST_DECIMAL_BASE));
        Count++;
        Value /= FS_VOLTEST_DECIMAL_BASE;
    }
    while (Count > 0) {
        Count--;
        gConsole.WriteLen(&Digits[Count], 1);
    }
}

static void PrintSigned(int Value) {
    if (Value < 0) {
        gConsole.Write("-");
        PrintUnsigned(0u - (UINT32)Value);
    } else {
        PrintUnsigned((UINT32)Value);
    }
}

static int FatVolumeCount(void) {
    int Count = 0;
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (gVols[i].Ops && !gVols[i].Ops->Synthetic) {
            Count++;
        }
    }
    return Count;
}

static void PrintVolume(int Idx) {
    char Letter[3];

    Letter[0] = (char)('A' + Idx);
    Letter[1] = ':';
    Letter[2] = 0;
    gConsole.Write("fs selftest: second volume ");
    gConsole.Write(Letter);
    gConsole.Write(" idx=");
    PrintUnsigned((UINT32)Idx);
    gConsole.Write(" count=");
    PrintUnsigned((UINT32)gVolCount);
    gConsole.Write(" fat=");
    PrintUnsigned((UINT32)FatVolumeCount());
    gConsole.Write(" via FileSystemSelectVolume\n");
}

static int CandidateVolume(void) {
    int i;

    for (i = 0; i < gVolCount; i++) {
        if (i == gDefaultVol || !gVols[i].HasBlocksId || gVols[i].BootFw) {
            continue;
        }
        if (!gVols[i].Ops || gVols[i].Ops->Synthetic) {
            continue;
        }
        return i;
    }
    return -1;
}

static void BuildPath(int Idx, char *Path) {
    static const char Name[] = "FSELFT2.BIN";
    UINT32 i;

    Path[0] = (char)('A' + Idx);
    Path[1] = ':';
    for (i = 0; i < (UINT32)sizeof(Name); i++) {
        Path[2u + i] = Name[i];
    }
}

static int DeleteIfPresent(const char *Path) {
    int Err = FileSystemDeleteFile(Path);

    if (Err == FAT_OK || Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return Err;
}

static int WritePattern(const char *Path) {
    UINT32 Offset;

    for (Offset = 0; Offset < FS_VOLTEST_BYTES; Offset++) {
        sVolTestBuffer[Offset] = PatternByte(Offset);
    }
    return FileSystemWriteFile(Path, sVolTestBuffer, FS_VOLTEST_BYTES);
}

static int VerifyReadBack(const char *Path) {
    UINTN Got = 0;
    UINT32 Offset;
    int Err = FileSystemReadFile(Path, sVolTestBuffer, sizeof(sVolTestBuffer),
                                 &Got);

    if (Err != FAT_OK) {
        return Err;
    }
    if (Got != FS_VOLTEST_BYTES) {
        return FAT_ERR_IO;
    }
    for (Offset = 0; Offset < FS_VOLTEST_BYTES; Offset++) {
        if (sVolTestBuffer[Offset] != PatternByte(Offset)) {
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

static int DeleteAndConfirm(const char *Path) {
    FAT_FILE_STAT Stat;
    int Err = FileSystemDeleteFile(Path);

    if (Err != FAT_OK) {
        return Err;
    }
    Err = FileSystemFileStat(Path, &Stat);
    if (Err == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return (Err == FAT_OK) ? FAT_ERR_IO : Err;
}

static int RunOnVolume(int Idx) {
    char Path[FS_VOLTEST_PATH_BYTES];
    int Err;

    Err = FileSystemSelectVolume(Idx);
    if (Err != FAT_OK) {
        return Err;
    }
    BuildPath(Idx, Path);
    Err = DeleteIfPresent(Path);
    if (Err == FAT_OK) {
        Err = WritePattern(Path);
    }
    if (Err == FAT_OK) {
        Err = VerifyReadBack(Path);
    }
    if (Err == FAT_OK) {
        Err = DeleteAndConfirm(Path);
    } else {
        (void)DeleteIfPresent(Path);
    }
    (void)FileSystemSelectVolume(-1);
    return Err;
}

int FsWriteSelfTestSecondVolume(void) {
    int Idx = CandidateVolume();
    int Err;

    if (Idx < 0) {
        return FAT_OK;
    }
    PrintVolume(Idx);
    Err = RunOnVolume(Idx);
    if (Err != FAT_OK) {
        gConsole.Write("FS SELFTEST VOL2 FAIL err=");
        PrintSigned(Err);
        gConsole.Write("\n");
        return Err;
    }
    gConsole.Write("FS SELFTEST VOL2 OK size=");
    PrintUnsigned(FS_VOLTEST_BYTES);
    gConsole.Write("\n");
    return FAT_OK;
}
