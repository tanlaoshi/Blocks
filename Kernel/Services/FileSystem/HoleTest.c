/*
 * HoleTest.c — `fs hole`：验证 FAT 分配提示不会漏掉前面的洞
 *
 * 场景：写 A → 写 B → 删 A → 写 C。提示（rover）在 B 之后，C 必须能
 * 绕过卷尾回到 A 留下的洞；近满卷则必须明确报 NOSPC，不能写坏 B。
 * 本文件只做判据与打印；命令口在 Shell/Commands/FileSystemHole.c。
 */
#include "FileSystem.h"
#include "CString.h"
#include "Console.h"
#include "PhysicalMemory.h"

#define FS_HOLE_A_BYTES            (1024u * 1024u)
#define FS_HOLE_B_BYTES            (1024u * 1024u)
#define FS_HOLE_C_BYTES            (2u * 1024u * 1024u)
#define FS_HOLE_PATTERN_FACTOR     31u
#define FS_HOLE_BYTE_MASK          0xFFu
/* 每轮换盐，读回时按偏移重算期望值，不必另存源数据 */
#define FS_HOLE_SALT_BASE          0x11u
#define FS_HOLE_SALT_STEP          0x20u
#define FS_HOLE_SALT_A             0u
#define FS_HOLE_SALT_B             1u
#define FS_HOLE_SALT_C             2u
#define FS_HOLE_PATH_MAX           32
#define FS_HOLE_DECIMAL_MAX        12
#define FS_HOLE_DECIMAL_BASE       10u
#define FS_HOLE_READ_OFFSET_NONE   0xFFFFFFFFu
#define FS_HOLE_BUFFER_PAGES       ((FS_HOLE_C_BYTES + PAGE_SIZE - 1) / PAGE_SIZE)
#define FS_HOLE_NAME_A             "FSHOLA.BIN"
#define FS_HOLE_NAME_B             "FSHOLB.BIN"
#define FS_HOLE_NAME_C             "FSHOLC.BIN"

typedef struct {
    UINT32 Size;       /* 读回后的实际文件大小 */
    UINT32 Offset;     /* 首个失配偏移；HasOffset=0 表示没走到逐字节比对 */
    UINT32 Want;
    UINT32 Got;
    int    HasOffset;
} FS_HOLE_READ;

static void HolePrintUnsigned(UINT32 Value) {
    char Digits[FS_HOLE_DECIMAL_MAX];
    int Count = 0;

    if (Value == 0u) {
        gConsole.Write("0");
        return;
    }
    while (Value != 0u && Count < FS_HOLE_DECIMAL_MAX) {
        Digits[Count] = (char)('0' + (int)(Value % FS_HOLE_DECIMAL_BASE));
        Count++;
        Value /= FS_HOLE_DECIMAL_BASE;
    }
    while (Count > 0) {
        Count--;
        gConsole.WriteLen(&Digits[Count], 1);
    }
}

static void HolePrintSigned(int Value) {
    if (Value < 0) {
        gConsole.Write("-");
        HolePrintUnsigned(0u - (UINT32)Value);
    } else {
        HolePrintUnsigned((UINT32)Value);
    }
}

/* 每一步都留返回码：判据要能逐条贴日志 */
static void HoleStep(const char *Tag, int Code) {
    gConsole.Write("  ");
    gConsole.Write(Tag);
    gConsole.Write(" rc=");
    HolePrintSigned(Code);
    gConsole.Write("\n");
}

static void HoleRoundHeader(int Round) {
    gConsole.Write("fs hole round ");
    HolePrintUnsigned((UINT32)Round);
    gConsole.Write(":\n");
}

static UINT8 HolePatternByte(UINT32 Offset, UINT8 Salt) {
    return (UINT8)((Offset * FS_HOLE_PATTERN_FACTOR + (UINT32)Salt) &
                   FS_HOLE_BYTE_MASK);
}

static void HoleFillPattern(UINT8 *Buffer, UINT32 Length, UINT8 Salt) {
    UINT32 Index;

    for (Index = 0; Index < Length; Index++) {
        Buffer[Index] = HolePatternByte(Index, Salt);
    }
}

static void HoleMakePath(char *Destination, UINTN Max, const char *Prefix,
                         const char *Leaf) {
    UINTN Index = 0;
    UINTN Source = 0;

    while (Prefix[Source] != 0 && Index + 1 < Max) {
        Destination[Index] = Prefix[Source];
        Index++;
        Source++;
    }
    Source = 0;
    while (Leaf[Source] != 0 && Index + 1 < Max) {
        Destination[Index] = Leaf[Source];
        Index++;
        Source++;
    }
    Destination[Index] = 0;
}

static int HoleDeleteIfExists(const char *Path) {
    int Code = FileSystemDeleteFile(Path);

    return (Code == FAT_ERR_NOENT) ? FAT_OK : Code;
}

static int HoleDeleteAll(const char *PathA, const char *PathB,
                         const char *PathC, int Report) {
    int Result = FAT_OK;
    int Code;

    Code = HoleDeleteIfExists(PathC);
    if (Report) {
        HoleStep("C delete", Code);
    }
    if (Code != FAT_OK) {
        Result = Code;
    }
    Code = HoleDeleteIfExists(PathB);
    if (Report) {
        HoleStep("B delete", Code);
    }
    if (Code != FAT_OK && Result == FAT_OK) {
        Result = Code;
    }
    Code = HoleDeleteIfExists(PathA);
    if (Report) {
        HoleStep("A delete", Code);
    }
    if (Code != FAT_OK && Result == FAT_OK) {
        Result = Code;
    }
    return Result;
}

static int HoleWritePattern(const char *Path, UINT8 *Buffer, UINT32 Bytes,
                            UINT8 Salt) {
    HoleFillPattern(Buffer, Bytes, Salt);
    return FileSystemWriteFile(Path, Buffer, Bytes);
}

static int HoleReadCheck(const char *Path, UINT8 *Buffer, UINT32 Bytes,
                         UINT8 Salt, FS_HOLE_READ *Read) {
    FAT_FILE_STAT Stat;
    UINTN Got = 0;
    UINT32 Index;
    int Code;

    Read->Size = 0;
    Read->Offset = FS_HOLE_READ_OFFSET_NONE;
    Read->Want = 0;
    Read->Got = 0;
    Read->HasOffset = 0;
    Code = FileSystemFileStat(Path, &Stat);
    if (Code != FAT_OK) {
        return Code;
    }
    Read->Size = Stat.Size;
    if (Stat.Size != Bytes) {
        return FAT_ERR_IO;
    }
    Code = FileSystemReadFile(Path, Buffer, Bytes, &Got);
    if (Code != FAT_OK) {
        return Code;
    }
    if (Got != Bytes) {
        Read->Size = (UINT32)Got;
        return FAT_ERR_IO;
    }
    for (Index = 0; Index < Bytes; Index++) {
        UINT8 Want = HolePatternByte(Index, Salt);

        if (Buffer[Index] != Want) {
            Read->Offset = Index;
            Read->Want = (UINT32)Want;
            Read->Got = (UINT32)Buffer[Index];
            Read->HasOffset = 1;
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

static int HoleVerifyAndReport(const char *Tag, const char *Path,
                               UINT8 *Buffer, UINT32 Bytes, UINT8 Salt) {
    FS_HOLE_READ Read;
    int Code = HoleReadCheck(Path, Buffer, Bytes, Salt, &Read);

    gConsole.Write("  ");
    gConsole.Write(Tag);
    gConsole.Write(" rc=");
    HolePrintSigned(Code);
    gConsole.Write(" bytes=");
    HolePrintUnsigned(Read.Size);
    if (Code == FAT_OK) {
        gConsole.Write(" match=yes\n");
    } else if (Read.HasOffset) {
        gConsole.Write(" mismatch=");
        HolePrintUnsigned(Read.Offset);
        gConsole.Write(" want=");
        gConsole.WriteHex32(Read.Want);
        gConsole.Write(" got=");
        gConsole.WriteHex32(Read.Got);
        gConsole.Write("\n");
    } else {
        gConsole.Write(" match=no\n");
    }
    return Code;
}

static int HoleCheckAbsent(const char *Path) {
    FAT_FILE_STAT Stat;
    int Code = FileSystemFileStat(Path, &Stat);

    if (Code == FAT_ERR_NOENT) {
        return FAT_OK;
    }
    return (Code == FAT_OK) ? FAT_ERR_EXIST : Code;
}

static int HoleSuccessFinish(const char *PathB, const char *PathC,
                             UINT8 *Buffer, UINT8 SaltB, UINT8 SaltC) {
    int Code = HoleVerifyAndReport("C readback", PathC, Buffer,
                                   FS_HOLE_C_BYTES, SaltC);

    if (Code != FAT_OK) {
        return Code;
    }
    return HoleVerifyAndReport("B readback", PathB, Buffer,
                               FS_HOLE_B_BYTES, SaltB);
}

static int HoleNoSpaceFinish(const char *PathB, const char *PathC,
                             UINT8 *Buffer, UINT8 SaltB, int WriteCode) {
    int Result = FAT_OK;
    int Code;

    if (WriteCode != FAT_ERR_NOSPC) {
        gConsole.Write("  C write expected no space\n");
        Result = (WriteCode == FAT_OK) ? FAT_ERR_EXIST : WriteCode;
    }
    Code = HoleVerifyAndReport("B readback", PathB, Buffer,
                               FS_HOLE_B_BYTES, SaltB);
    if (Code != FAT_OK && Result == FAT_OK) {
        Result = Code;
    }
    Code = HoleCheckAbsent(PathC);
    HoleStep("C absent", Code);
    if (Code != FAT_OK && Result == FAT_OK) {
        Result = Code;
    }
    return Result;
}

static int HoleRoundBody(int Round, int ExpectNoSpace,
                         UINT8 *Buffer, const char *PathA, const char *PathB,
                         const char *PathC) {
    UINT8 SaltBase = (UINT8)(FS_HOLE_SALT_BASE +
                             (UINT32)(Round - 1) * FS_HOLE_SALT_STEP);
    UINT8 SaltA = (UINT8)(SaltBase + FS_HOLE_SALT_A);
    UINT8 SaltB = (UINT8)(SaltBase + FS_HOLE_SALT_B);
    UINT8 SaltC = (UINT8)(SaltBase + FS_HOLE_SALT_C);
    int Code;

    HoleRoundHeader(Round);
    Code = HoleDeleteAll(PathA, PathB, PathC, 0);
    if (Code != FAT_OK) {
        HoleStep("pre-clean", Code);
        return Code;
    }

    Code = HoleWritePattern(PathA, Buffer, FS_HOLE_A_BYTES, SaltA);
    HoleStep("A write", Code);
    if (Code != FAT_OK) {
        return Code;
    }
    Code = HoleWritePattern(PathB, Buffer, FS_HOLE_B_BYTES, SaltB);
    HoleStep("B write", Code);
    if (Code != FAT_OK) {
        return Code;
    }
    Code = FileSystemDeleteFile(PathA);
    HoleStep("A delete", Code);
    if (Code != FAT_OK) {
        return Code;
    }
    Code = HoleCheckAbsent(PathA);
    HoleStep("A absent", Code);
    if (Code != FAT_OK) {
        return Code;
    }

    Code = HoleWritePattern(PathC, Buffer, FS_HOLE_C_BYTES, SaltC);
    HoleStep("C write", Code);
    if (ExpectNoSpace) {
        return HoleNoSpaceFinish(PathB, PathC, Buffer, SaltB, Code);
    }
    if (Code != FAT_OK) {
        return Code;
    }
    return HoleSuccessFinish(PathB, PathC, Buffer, SaltB, SaltC);
}

static int HoleRound(int Round, int ExpectNoSpace, UINT8 *Buffer,
                     const char *PathA, const char *PathB, const char *PathC) {
    int Code = HoleRoundBody(Round, ExpectNoSpace, Buffer,
                             PathA, PathB, PathC);
    int Cleanup = HoleDeleteAll(PathA, PathB, PathC, 1);

    return (Code != FAT_OK) ? Code : Cleanup;
}

int FileSystemHoleTest(const char *Prefix, int Rounds, int ExpectNoSpace) {
    char PathA[FS_HOLE_PATH_MAX];
    char PathB[FS_HOLE_PATH_MAX];
    char PathC[FS_HOLE_PATH_MAX];
    UINT8 *Buffer;
    int Round;
    int Code = FAT_OK;

    if (Prefix == 0) {
        Prefix = "";
    }
    if (Rounds < 1 || Rounds > FS_HOLE_ROUNDS_MAX) {
        gConsole.Write("fs hole: rounds must be 1..");
        HolePrintUnsigned(FS_HOLE_ROUNDS_MAX);
        gConsole.Write("\n");
        return FAT_ERR_INVAL;
    }
    HoleMakePath(PathA, sizeof(PathA), Prefix, FS_HOLE_NAME_A);
    HoleMakePath(PathB, sizeof(PathB), Prefix, FS_HOLE_NAME_B);
    HoleMakePath(PathC, sizeof(PathC), Prefix, FS_HOLE_NAME_C);
    gConsole.Write("fs hole: prefix=\"");
    gConsole.Write(Prefix);
    gConsole.Write("\" rounds=");
    HolePrintUnsigned((UINT32)Rounds);
    gConsole.Write(ExpectNoSpace ? " expect=nospace\n" : " expect=space\n");

    Buffer = (UINT8 *)PhysicalMemoryAllocatePages(FS_HOLE_BUFFER_PAGES);
    if (Buffer == 0) {
        gConsole.Write("fs hole: buffer alloc failed\n");
        return FAT_ERR_IO;
    }
    for (Round = 1; Round <= Rounds; Round++) {
        Code = HoleRound(Round, ExpectNoSpace, Buffer,
                         PathA, PathB, PathC);
        if (Code != FAT_OK) {
            break;
        }
    }
    PhysicalMemoryFreePages(Buffer, FS_HOLE_BUFFER_PAGES);
    /* 操作按前缀切卷；跑完还给默认卷，免后续 log 落到测试卷上 */
    (void)FileSystemActivate(FileSystemDefaultVol());
    if (Code == FAT_OK) {
        gConsole.Write("FS HOLE OK");
        if (ExpectNoSpace) {
            gConsole.Write(" (C rejected with NOSPC; B intact)");
        } else {
            gConsole.Write(" (A hole reused; B intact)");
        }
        gConsole.Write("\n");
    } else {
        gConsole.Write("FS HOLE FAIL rc=");
        HolePrintSigned(Code);
        gConsole.Write("\n");
    }
    return Code;
}
