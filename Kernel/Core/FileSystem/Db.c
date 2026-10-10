/*
 * Db.c — BLOCKS.DB 文本 KV（K46）
 *
 * 【初学者】
 * 内存表 + 整文件重写；格式 key=value（对标现网 DB1 文本形）。
 * 产品名 BLOCKS.DB（不用 TOYOS）。不依赖 Gui/Theme。
 */
#include "Db.h"
#include "FatFile.h"
#include "HalSerial.h"
#include "SerialConfig.h"

typedef struct {
    int Used;
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
} DB_REC;

static DB_REC gRecs[DB_MAX_RECORDS];
static int gReady;

static int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static int StrEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

static void CopyStr(char *Dst, int Cap, const char *Src) {
    int i;
    if (Dst == 0 || Cap <= 0) {
        return;
    }
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < Cap && Src[i]; i++) {
        Dst[i] = Src[i];
    }
    Dst[i] = 0;
}

/*
 * KeyOk — 键为可见 ASCII，不含空格与 '='
 * 谁调用：本文件 ApplyLine / DbGet / DbSet。
 */
static int KeyOk(const char *Key) {
    int i;
    if (Key == 0 || Key[0] == 0) {
        return 0;
    }
    for (i = 0; Key[i]; i++) {
        char C = Key[i];
        if (C <= 32 || C >= 127 || C == '=') {
            return 0;
        }
        if (i + 1 >= DB_KEY_MAX) {
            return 0;
        }
    }
    return 1;
}

static int FindSlot(const char *Key) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (gRecs[i].Used && StrEq(gRecs[i].Key, Key)) {
            return i;
        }
    }
    return -1;
}

static int AllocSlot(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            return i;
        }
    }
    return -1;
}

static void ClearAll(void) {
    int i;
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        gRecs[i].Used = 0;
        gRecs[i].Key[0] = 0;
        gRecs[i].Val[0] = 0;
    }
}

/*
 * ApplyLine — 解析一行 key=value 填入表
 * 谁调用：DbLoad 扫缓冲。
 */
static void ApplyLine(const char *Line) {
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    int Ki = 0;
    int Vi = 0;
    const char *P = Line;
    int Slot;

    while (*P && IsSpace(*P)) {
        P++;
    }
    if (!*P || *P == '#') {
        return;
    }
    while (*P && *P != '=' && !IsSpace(*P) && Ki < DB_KEY_MAX - 1) {
        Key[Ki++] = *P++;
    }
    Key[Ki] = 0;
    while (*P && IsSpace(*P)) {
        P++;
    }
    if (*P != '=') {
        return;
    }
    P++;
    while (*P && IsSpace(*P)) {
        P++;
    }
    while (*P && *P != '\n' && *P != '\r' && Vi < DB_VAL_MAX - 1) {
        Val[Vi++] = *P++;
    }
    while (Vi > 0 && IsSpace(Val[Vi - 1])) {
        Vi--;
    }
    Val[Vi] = 0;
    if (!KeyOk(Key)) {
        return;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return;
    }
    gRecs[Slot].Used = 1;
    CopyStr(gRecs[Slot].Key, DB_KEY_MAX, Key);
    CopyStr(gRecs[Slot].Val, DB_VAL_MAX, Val);
}

/*
 * DbLoad — 从 BLOCKS.DB 装填内存表
 * 谁调用：DbInitialize。无文件则空表仍 OK。
 * 前后文：后 — gReady=1；兄弟 FatFileReadPath
 */
static int DbLoad(void) {
    static char Buf[4096];
    UINT32 Size = 0;
    UINT32 i;
    char Line[DB_KEY_MAX + DB_VAL_MAX + 4];
    UINT32 L;

    ClearAll();
    /* FatFileReadPath：成功返回字节数（≥0），失败 -1——勿用 != 0 */
    if (FatFileReadPath(DB_PATH, Buf, sizeof(Buf) - 1u, &Size) < 0 || Size == 0) {
        return DB_NOENT;
    }
    Buf[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                ApplyLine(Line);
                L = 0;
            }
            continue;
        }
        if (L + 1u < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return DB_OK;
}

/*
 * DbSave — 整表重写 BLOCKS.DB
 * 谁调用：DbSet 成功路径。
 */
static int DbSave(void) {
    static char Buf[4096];
    UINT32 N = 0;
    int i;
    int k;

    Buf[N++] = '#';
    Buf[N++] = ' ';
    Buf[N++] = 'B';
    Buf[N++] = 'L';
    Buf[N++] = 'O';
    Buf[N++] = 'C';
    Buf[N++] = 'K';
    Buf[N++] = 'S';
    Buf[N++] = '.';
    Buf[N++] = 'D';
    Buf[N++] = 'B';
    Buf[N++] = '\n';
    for (i = 0; i < DB_MAX_RECORDS; i++) {
        if (!gRecs[i].Used) {
            continue;
        }
        for (k = 0; gRecs[i].Key[k] && N + 2u < sizeof(Buf); k++) {
            Buf[N++] = gRecs[i].Key[k];
        }
        if (N + 1u >= sizeof(Buf)) {
            return DB_ERR;
        }
        Buf[N++] = '=';
        for (k = 0; gRecs[i].Val[k] && N + 2u < sizeof(Buf); k++) {
            Buf[N++] = gRecs[i].Val[k];
        }
        if (N + 1u >= sizeof(Buf)) {
            return DB_ERR;
        }
        Buf[N++] = '\n';
    }
    /*
     * vvfat/virtio：就地改已有文件时，偶发「簇数据已新、目录 Size 仍旧」。
     * 读侧按 Size 截断 → cat 只见首行；dbget 走内存仍正常。
     * 对策：先 rm 再 create，强制新目录项带正确 Size。
     */
    (void)FatRmPath(DB_PATH);
    if (FatFileWritePath(DB_PATH, Buf, N) != 0) {
        return DB_ERR;
    }
    return DB_OK;
}

int DbInitialize(void) {
    int Rc;

    Rc = DbLoad();
    gReady = 1;
    if (Rc == DB_OK) {
        HalSerialWriteChannel(SLOG_FS, "Db: BLOCKS.DB loaded\n");
    } else {
        HalSerialWriteChannel(SLOG_FS, "Db: empty (no BLOCKS.DB yet)\n");
    }
    return 0;
}

int DbGet(const char *Key, char *Out, UINTN OutMax) {
    int Slot;

    if (!gReady) {
        (void)DbInitialize();
    }
    if (!KeyOk(Key) || Out == 0 || OutMax == 0) {
        return DB_INVAL;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        return DB_NOENT;
    }
    CopyStr(Out, (int)OutMax, gRecs[Slot].Val);
    return DB_OK;
}

int DbSet(const char *Key, const char *Value) {
    int Slot;

    if (!gReady) {
        (void)DbInitialize();
    }
    if (!KeyOk(Key) || Value == 0) {
        return DB_INVAL;
    }
    if (Value[0] == 0) {
        Slot = FindSlot(Key);
        if (Slot < 0) {
            return DB_NOENT;
        }
        gRecs[Slot].Used = 0;
        gRecs[Slot].Key[0] = 0;
        gRecs[Slot].Val[0] = 0;
        return DbSave();
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return DB_FULL;
    }
    gRecs[Slot].Used = 1;
    CopyStr(gRecs[Slot].Key, DB_KEY_MAX, Key);
    CopyStr(gRecs[Slot].Val, DB_VAL_MAX, Value);
    return DbSave();
}
