/*
 * DataBase.c — BLOCKS.DB 文本 KV（K46）
 *
 * 【初学者】
 * 内存表 + 整文件重写；格式 key=value（对标现网 DB1 文本形）。
 * 产品名 BLOCKS.DB（不用 TOYOS）。不依赖 Gui/Theme。
 */
#include "DataBase.h"
#include "FatFile.h"
#include "HalSerial.h"
#include "SerialConfig.h"

typedef struct {
    int Used;
    char Key[DATA_BASE_KEY_MAX];
    char Value[DATA_BASE_VALUE_MAX];
} DATA_BASE_RECORD;

static DATA_BASE_RECORD gRecords[DATA_BASE_MAX_RECORDS];
static int gReady;

static int IsSpace(char C) {
    return C == ' ' || C == '\t' || C == '\r' || C == '\n';
}

static int StringsEqual(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B && *A == *B) {
        A++;
        B++;
    }
    return *A == 0 && *B == 0;
}

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

/*
 * KeyOk — 键为可见 ASCII，不含空格与 '='
 * 谁调用：本文件 ApplyLine / DataBaseGet / DataBaseSet。
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
        if (i + 1 >= DATA_BASE_KEY_MAX) {
            return 0;
        }
    }
    return 1;
}

static int FindSlot(const char *Key) {
    int i;
    for (i = 0; i < DATA_BASE_MAX_RECORDS; i++) {
        if (gRecords[i].Used && StringsEqual(gRecords[i].Key, Key)) {
            return i;
        }
    }
    return -1;
}

static int AllocSlot(void) {
    int i;
    for (i = 0; i < DATA_BASE_MAX_RECORDS; i++) {
        if (!gRecords[i].Used) {
            return i;
        }
    }
    return -1;
}

static void ClearAll(void) {
    int i;
    for (i = 0; i < DATA_BASE_MAX_RECORDS; i++) {
        gRecords[i].Used = 0;
        gRecords[i].Key[0] = 0;
        gRecords[i].Value[0] = 0;
    }
}

/*
 * ApplyLine — 解析一行 key=value 填入表
 * 谁调用：DataBaseLoad 扫缓冲。
 */
static void ApplyLine(const char *Line) {
    char Key[DATA_BASE_KEY_MAX];
    char Value[DATA_BASE_VALUE_MAX];
    int KeyIndex = 0;
    int ValueIndex = 0;
    const char *P = Line;
    int Slot;

    while (*P && IsSpace(*P)) {
        P++;
    }
    if (!*P || *P == '#') {
        return;
    }
    while (*P && *P != '=' && !IsSpace(*P) && KeyIndex < DATA_BASE_KEY_MAX - 1) {
        Key[KeyIndex++] = *P++;
    }
    Key[KeyIndex] = 0;
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
    while (*P && *P != '\n' && *P != '\r' && ValueIndex < DATA_BASE_VALUE_MAX - 1) {
        Value[ValueIndex++] = *P++;
    }
    while (ValueIndex > 0 && IsSpace(Value[ValueIndex - 1])) {
        ValueIndex--;
    }
    Value[ValueIndex] = 0;
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
    gRecords[Slot].Used = 1;
    StringCopy(gRecords[Slot].Key, DATA_BASE_KEY_MAX, Key);
    StringCopy(gRecords[Slot].Value, DATA_BASE_VALUE_MAX, Value);
}

/*
 * DataBaseLoad — 从 BLOCKS.DB 装填内存表
 * 谁调用：DataBaseInitialize。无文件则空表仍 OK。
 * 前后文：后 — gReady=1；兄弟 FatFileReadPath
 */
static int DataBaseLoad(void) {
    static char Buffer[4096];
    UINT32 Size = 0;
    UINT32 i;
    char Line[DATA_BASE_KEY_MAX + DATA_BASE_VALUE_MAX + 4];
    UINT32 L;

    ClearAll();
    /* FatFileReadPath：成功返回字节数（≥0），失败 -1——勿用 != 0 */
    if (FatFileReadPath(DATA_BASE_PATH, Buffer, sizeof(Buffer) - 1u, &Size) < 0 || Size == 0) {
        return DATA_BASE_NOENT;
    }
    Buffer[Size] = 0;
    L = 0;
    for (i = 0; i <= Size; i++) {
        char C = (i < Size) ? Buffer[i] : '\n';
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
    return DATA_BASE_OK;
}

/*
 * DataBaseSave — 整表重写 BLOCKS.DB
 * 谁调用：DataBaseSet 成功路径。
 */
static int DataBaseSave(void) {
    static char Buffer[4096];
    UINT32 N = 0;
    int i;
    int k;

    Buffer[N++] = '#';
    Buffer[N++] = ' ';
    Buffer[N++] = 'B';
    Buffer[N++] = 'L';
    Buffer[N++] = 'O';
    Buffer[N++] = 'C';
    Buffer[N++] = 'K';
    Buffer[N++] = 'S';
    Buffer[N++] = '.';
    Buffer[N++] = 'D';
    Buffer[N++] = 'B';
    Buffer[N++] = '\n';
    for (i = 0; i < DATA_BASE_MAX_RECORDS; i++) {
        if (!gRecords[i].Used) {
            continue;
        }
        for (k = 0; gRecords[i].Key[k] && N + 2u < sizeof(Buffer); k++) {
            Buffer[N++] = gRecords[i].Key[k];
        }
        if (N + 1u >= sizeof(Buffer)) {
            return DATA_BASE_ERR;
        }
        Buffer[N++] = '=';
        for (k = 0; gRecords[i].Value[k] && N + 2u < sizeof(Buffer); k++) {
            Buffer[N++] = gRecords[i].Value[k];
        }
        if (N + 1u >= sizeof(Buffer)) {
            return DATA_BASE_ERR;
        }
        Buffer[N++] = '\n';
    }
    /*
     * vvfat/virtio：就地改已有文件时，偶发「簇数据已新、目录 Size 仍旧」。
     * 读侧按 Size 截断 → cat 只见首行；dbget 走内存仍正常。
     * 对策：先 rm 再 create，强制新目录项带正确 Size。
     */
    (void)FatRmPath(DATA_BASE_PATH);
    if (FatFileWritePath(DATA_BASE_PATH, Buffer, N) != 0) {
        return DATA_BASE_ERR;
    }
    return DATA_BASE_OK;
}

int DataBaseInitialize(void) {
    int Rc;

    Rc = DataBaseLoad();
    gReady = 1;
    if (Rc == DATA_BASE_OK) {
        HalSerialWriteChannel(SLOG_FS, "DataBase: BLOCKS.DB loaded\n");
    } else {
        HalSerialWriteChannel(SLOG_FS, "DataBase: empty (no BLOCKS.DB yet)\n");
    }
    return 0;
}

int DataBaseGet(const char *Key, char *Out, UINTN OutMax) {
    int Slot;

    if (!gReady) {
        (void)DataBaseInitialize();
    }
    if (!KeyOk(Key) || Out == 0 || OutMax == 0) {
        return DATA_BASE_INVAL;
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        return DATA_BASE_NOENT;
    }
    StringCopy(Out, (int)OutMax, gRecords[Slot].Value);
    return DATA_BASE_OK;
}

int DataBaseSet(const char *Key, const char *Value) {
    int Slot;

    if (!gReady) {
        (void)DataBaseInitialize();
    }
    if (!KeyOk(Key) || Value == 0) {
        return DATA_BASE_INVAL;
    }
    if (Value[0] == 0) {
        Slot = FindSlot(Key);
        if (Slot < 0) {
            return DATA_BASE_NOENT;
        }
        gRecords[Slot].Used = 0;
        gRecords[Slot].Key[0] = 0;
        gRecords[Slot].Value[0] = 0;
        return DataBaseSave();
    }
    Slot = FindSlot(Key);
    if (Slot < 0) {
        Slot = AllocSlot();
    }
    if (Slot < 0) {
        return DATA_BASE_FULL;
    }
    gRecords[Slot].Used = 1;
    StringCopy(gRecords[Slot].Key, DATA_BASE_KEY_MAX, Key);
    StringCopy(gRecords[Slot].Value, DATA_BASE_VALUE_MAX, Value);
    return DataBaseSave();
}
