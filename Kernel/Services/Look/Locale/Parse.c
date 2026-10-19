/*
 * LocaleParse.c — PR-S3-locale-1：catalog 文本解析与装载
 */
#include "LocalePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "PhysicalMemory.h"
#include "Debug.h"

static void CopyStrUnescape(char *Dst, UINTN DstMax, const char *Src) {
    UINTN i;
    UINTN o;

    if (!Dst || DstMax == 0) {
        return;
    }
    if (!Src) {
        Dst[0] = 0;
        return;
    }
    o = 0;
    for (i = 0; Src[i] && o + 1 < DstMax; i++) {
        if (Src[i] == '\\' && Src[i + 1] == 'n') {
            Dst[o++] = '\n';
            i++;
        } else if (Src[i] == '\\' && Src[i + 1] == '\\') {
            Dst[o++] = '\\';
            i++;
        } else {
            Dst[o++] = Src[i];
        }
    }
    Dst[o] = 0;
}

static void ResetToFallback(void) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        CopyStrUnescape(gEn[i], LOCALE_STR_MAX, gEnFallback[i]);
        CopyStrUnescape(gZh[i], LOCALE_STR_MAX, gZhFallback[i]);
    }
}

static int KeyToId(const char *Key) {
    UINT32 i;

    for (i = 0; i < (UINT32)MSG_COUNT; i++) {
        const char *K = gMsgKeys[i];
        UINTN j;
        for (j = 0; K[j] && Key[j] && K[j] == Key[j]; j++) {
        }
        if (K[j] == 0 && Key[j] == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void ApplyLine(char (*Table)[LOCALE_STR_MAX], const char *Line) {
    char Key[48];
    const char *Eq;
    const char *Val;
    UINTN i;
    int Id;

    while (*Line == ' ' || *Line == '\t') {
        Line++;
    }
    if (*Line == 0 || *Line == '#' || *Line == ';') {
        return;
    }
    Eq = Line;
    while (*Eq && *Eq != '=') {
        Eq++;
    }
    if (*Eq != '=' || Eq == Line) {
        return;
    }
    i = 0;
    while (Line < Eq && i + 1 < sizeof(Key)) {
        if (*Line != ' ' && *Line != '\t') {
            Key[i++] = *Line;
        }
        Line++;
    }
    Key[i] = 0;
    Val = Eq + 1;
    while (*Val == ' ' || *Val == '\t') {
        Val++;
    }
    Id = KeyToId(Key);
    if (Id < 0) {
        return;
    }
    CopyStrUnescape(Table[Id], LOCALE_STR_MAX, Val);
}

static int LoadCatalogFile(const char *Path, char (*Table)[LOCALE_STR_MAX]) {
    UINT8 *Buf;
    UINT32 Pages;
    UINTN Size;
    UINTN i;
    UINTN LineStart;
    int Err;

    Pages = (LOCALE_FILE_MAX + 4095u) / 4096u;
    Buf = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (!Buf) {
        return -1;
    }
    Size = 0;
    Err = FileSystemReadFile(Path, Buf, LOCALE_FILE_MAX - 1, &Size);
    if (Err != FAT_OK || Size == 0) {
        PhysicalMemoryFreePages(Buf, Pages);
        return -1;
    }
    Buf[Size] = 0;
    LineStart = 0;
    for (i = 0; i <= Size; i++) {
        if (i == Size || Buf[i] == '\n' || Buf[i] == '\r') {
            char Saved = (char)Buf[i];
            Buf[i] = 0;
            if (i > LineStart) {
                ApplyLine(Table, (const char *)&Buf[LineStart]);
            }
            Buf[i] = (UINT8)Saved;
            if (i < Size && Buf[i] == '\r' && Buf[i + 1] == '\n') {
                i++;
            }
            LineStart = i + 1;
        }
    }
    PhysicalMemoryFreePages(Buf, Pages);
    return 0;
}

static int CodeFromName(const char *Name, char *Code, int CodeMax) {
    int n;
    int i;
    char c;

    n = 0;
    while (Name[n] != 0) {
        n++;
    }
    if (n < 5 || Name[n - 4] != '.') {
        return -1;
    }
    if (!((Name[n - 3] == 't' || Name[n - 3] == 'T') &&
          (Name[n - 2] == 'x' || Name[n - 2] == 'X') &&
          (Name[n - 1] == 't' || Name[n - 1] == 'T'))) {
        return -1;
    }
    if (n - 4 <= 0 || n - 4 >= CodeMax) {
        return -1;
    }
    for (i = 0; i < n - 4; i++) {
        c = Name[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return -1;
        }
        Code[i] = c;
    }
    Code[i] = 0;
    return 0;
}

void LocaleLoadCatalogs(void) {
    FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    int Count;
    int i;
    int FromFile;

    LocaleRegistryReset();
    ResetToFallback();
    Count = 0;
    FromFile = 0;
    if (FileSystemListEntries("Assets/Locale", Ents, FAT_LIST_MAX, &Count) != FAT_OK) {
        DebugWrite("locale: using built-in strings (no Assets/Locale)\n");
        return;
    }
    for (i = 0; i < Count; i++) {
        char Code[8];
        char Path[40];
        int Index;
        int p;
        const char *Prefix = "Assets/Locale/";

        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (CodeFromName(Ents[i].Name, Code, (int)sizeof(Code)) != 0) {
            continue;
        }
        Index = LocaleOffer(Code);
        if (Index < 0 || LocaleLive(Index) == 0) {
            continue;
        }
        p = 0;
        while (Prefix[p] != 0 && p + 1 < (int)sizeof(Path)) {
            Path[p] = Prefix[p];
            p++;
        }
        {
            int c = 0;
            while (Code[c] != 0 && p + 1 < (int)sizeof(Path)) {
                Path[p++] = Code[c++];
            }
        }
        if (p + 4 >= (int)sizeof(Path)) {
            continue;
        }
        Path[p++] = '.';
        Path[p++] = 't';
        Path[p++] = 'x';
        Path[p++] = 't';
        Path[p] = 0;
        if (LoadCatalogFile(Path, LocaleLive(Index)) != 0) {
            LocaleDropLast(Index);
            continue;
        }
        if (!FromFile) {
            DebugWrite("locale: catalog ");
        }
        DebugWrite(Code);
        DebugWrite(" ");
        FromFile = 1;
    }
    if (FromFile) {
        DebugWrite("from Assets/Locale\n");
    } else {
        DebugWrite("locale: using built-in strings (no Assets/Locale)\n");
    }
}
