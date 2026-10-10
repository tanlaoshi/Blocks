/*
 * DataBase.c — BLOCKS.DB 文本 KV（K46）
 *
 * 【初学者】
 * - 分层：Core/FileSystem；落盘经 FatFileWritePath
 * - 对外：DataBaseInitialize / DataBaseGet / DataBaseSet
 * - 边界：内存表 + 整文件重写 key=value；不依赖 Gui/Theme
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

static int AllocateSlot(void) {
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
 *
 * 做什么：跳过空白与 # 注释；拆 Key/Value 写入 gRecords。
 * 谁调用：仅 DataBaseLoad。
 * 返回：void
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
        Slot = AllocateSlot();
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
 *
 * 做什么：FatFileReadPath 读文件；按行 ApplyLine。
 * 谁调用：仅 DataBaseInitialize。
 * 前后文：兄弟 — FatFileReadPath；后 — gReady。
 * 返回：DATA_BASE_OK / DATA_BASE_NOENT
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
 *
 * 做什么：序列化 gRecords；FatDeleteFile 再 FatFileWritePath（vvfat Size 对策）。
 * 谁调用：DataBaseSet。
 * 返回：DATA_BASE_OK / DATA_BASE_ERR
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
    (void)FatDeleteFile(DATA_BASE_PATH);
    if (FatFileWritePath(DATA_BASE_PATH, Buffer, N) != 0) {
        return DATA_BASE_ERR;
    }
    return DATA_BASE_OK;
}

/*
 * DataBaseInitialize — 启动时装 BLOCKS.DB（可空）
 *
 * 做什么：DataBaseLoad；置 gReady；打串口一行状态。
 * 谁调用：FileSystemInitialize；Store/DataBaseGet 懒加载。
 * 返回：0
 */
int DataBaseInitialize(void) {
    int LoadResult;

    LoadResult = DataBaseLoad();
    gReady = 1;
    if (LoadResult == DATA_BASE_OK) {
        HalSerialWriteChannel(SLOG_FS, "DataBase: BLOCKS.DB loaded\n");
    } else {
        HalSerialWriteChannel(SLOG_FS, "DataBase: empty (no BLOCKS.DB yet)\n");
    }
    return 0;
}

/*
 * DataBaseGet — 按键读 value
 *
 * 做什么：查内存表；Out 以 NUL 结尾。
 * 谁调用：Shell `dbget`；Theme/Settings 读配置。
 * 返回：DATA_BASE_OK / NOENT / INVAL
 */
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

/*
 * DataBaseSet — 写键值并整文件落盘
 *
 * 做什么：更新表；空 Value 删键；DataBaseSave（先删后写文件）。
 * 谁调用：Shell `dbset`；Store MarkInstalled；Theme 保存。
 * 前后文：后 — FatDeleteFile + FatFileWritePath。
 * 返回：DATA_BASE_* 
 */
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
        Slot = AllocateSlot();
    }
    if (Slot < 0) {
        return DATA_BASE_FULL;
    }
    gRecords[Slot].Used = 1;
    StringCopy(gRecords[Slot].Key, DATA_BASE_KEY_MAX, Key);
    StringCopy(gRecords[Slot].Value, DATA_BASE_VALUE_MAX, Value);
    return DataBaseSave();
}
