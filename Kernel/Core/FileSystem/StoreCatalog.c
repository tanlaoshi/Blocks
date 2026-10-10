/*
 * StoreCatalog.c — 解析 / 加载 STORE.CAT
 *
 * 【初学者】
 * - 分层：Core/FileSystem
 * - 对外：StoreLoadCatalog / StoreHostArch
 * - 无 STORE.CAT 时用内建 demo 一行
 */
#include "Store.h"
#include "FatFile.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define STORE_CATALOG_MAX 4096u

static void StringCopy(char *Destination, int Capacity, const char *Source) {
    int i;
    if (Destination == 0 || Capacity <= 0) {
        return;
    }
    if (Source == 0) {
        Destination[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Capacity && Source[i]; i++) {
        Destination[i] = Source[i];
    }
    Destination[i] = 0;
}

static void TokenCopy(char *Destination, int Capacity, const char *Start, const char *End) {
    int n = 0;
    while (Start < End && (*Start == ' ' || *Start == '\t')) {
        Start++;
    }
    while (End > Start && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '\r')) {
        End--;
    }
    while (Start < End && n + 1 < Capacity) {
        Destination[n++] = *Start++;
    }
    if (Capacity > 0) {
        Destination[n] = 0;
    }
}

/*
 * StoreHostArch — 本机架构字符串（catalog Arch 字段匹配）
 *
 * 做什么：编译期选 x86_64 / arm64 / riscv64 / any。
 * 谁调用：StoreInstall。
 * 返回：静态字符串
 */
const char *StoreHostArch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__riscv)
    return "riscv64";
#else
    return "any";
#endif
}

static int ParseLine(STORE_ENTRY *Entry, const char *Line) {
    const char *P;
    const char *Starts[8];
    const char *Fields[8];
    int N = 0;
    char Ver[16];
    UINT32 VersionNumber = 0;
    const char *S;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#') {
        return -1;
    }
    P = Line;
    Starts[0] = P;
    while (*P && N < 7) {
        if (*P == '|') {
            Fields[N] = P;
            N++;
            if (N < 7) {
                Starts[N] = P + 1;
            }
        }
        P++;
    }
    if (N != 6) {
        return -1;
    }
    Fields[6] = P;
    TokenCopy(Entry->Id, STORE_ID_MAX, Starts[0], Fields[0]);
    TokenCopy(Entry->Type, (int)sizeof(Entry->Type), Starts[1], Fields[1]);
    TokenCopy(Ver, (int)sizeof(Ver), Starts[2], Fields[2]);
    S = Ver;
    while (*S >= '0' && *S <= '9') {
        VersionNumber = VersionNumber * 10u + (UINT32)(*S - '0');
        S++;
    }
    Entry->Version = VersionNumber;
    TokenCopy(Entry->File, STORE_FILE_MAX, Starts[3], Fields[3]);
    TokenCopy(Entry->Arch, STORE_ARCH_MAX, Starts[5], Fields[5]);
    TokenCopy(Entry->Title, STORE_TITLE_MAX, Starts[6], Fields[6]);
    if (Entry->Id[0] == 0 || Entry->File[0] == 0) {
        return -1;
    }
    if (Entry->Type[0] == 0) {
        StringCopy(Entry->Type, (int)sizeof(Entry->Type), "app");
    }
    return 0;
}

static int LoadFromBuffer(const char *Buffer, UINTN Size, STORE_ENTRY *Out, int Max, int *OutCount) {
    UINTN i;
    UINTN LineStart = 0;
    int Count = 0;

    *OutCount = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buffer[i] == '\n' || Buffer[i] == '\r') {
            char Line[192];
            UINTN Length = i - LineStart;
            UINTN k;
            if (Length > 0 && Count < Max) {
                if (Length >= sizeof(Line)) {
                    Length = sizeof(Line) - 1;
                }
                for (k = 0; k < Length; k++) {
                    Line[k] = Buffer[LineStart + k];
                }
                Line[Length] = 0;
                if (ParseLine(&Out[Count], Line) == 0) {
                    Count++;
                }
            }
            if (i < Size && Buffer[i] == '\r' && i + 1 < Size && Buffer[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    *OutCount = Count;
    return STORE_OK;
}

static int LoadBuiltin(STORE_ENTRY *Out, int Max, int *OutCount) {
    static const char Builtin[] =
        "demo|asset|1|SPKG.TXT|-|any|Store demo pack\n";
    return LoadFromBuffer(Builtin, sizeof(Builtin) - 1, Out, Max, OutCount);
}

/*
 * StoreLoadCatalog — 读 STORE.CAT 或回退内建清单
 *
 * 做什么：FatFileReadPath → ParseLine；失败用 Builtin。
 * 谁调用：StoreInstall；Shell `store list`。
 * 返回：STORE_OK / STORE_INVAL / STORE_NOSPC
 */
int StoreLoadCatalog(STORE_ENTRY *Out, int Max, int *OutCount) {
    UINT8 *Buffer;
    UINT32 Pages;
    UINT32 Size = 0;
    int Result;

    if (Out == 0 || Max <= 0 || OutCount == 0) {
        return STORE_INVAL;
    }
    *OutCount = 0;
    Pages = (STORE_CATALOG_MAX + 4095u) / 4096u;
    Buffer = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (Buffer == 0) {
        return STORE_NOSPC;
    }
    Result = FatFileReadPath(STORE_CATALOG_PATH, Buffer, STORE_CATALOG_MAX - 1u, &Size);
    if (Result < 0 || Size == 0) {
        PhysicalMemoryFreePages(Buffer, Pages);
        HalSerialWriteChannel(SLOG_FS, "Store: catalog builtin\n");
        return LoadBuiltin(Out, Max, OutCount);
    }
    Buffer[Size] = 0;
    Result = LoadFromBuffer((const char *)Buffer, Size, Out, Max, OutCount);
    PhysicalMemoryFreePages(Buffer, Pages);
    if (Result != STORE_OK || *OutCount == 0) {
        HalSerialWriteChannel(SLOG_FS, "Store: catalog builtin\n");
        return LoadBuiltin(Out, Max, OutCount);
    }
    return STORE_OK;
}
