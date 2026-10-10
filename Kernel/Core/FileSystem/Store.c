/*
 * Store.c — K47：按 id 装一包（根目录 8.3）
 *
 * 【初学者】
 * 源=清单 File；目标=A+id 截断+扩展名；BLOCKS.DB 记 si.<id>=type|dst。
 * catalog 解析见 StoreCatalog.c。不做 StoreUi/网络/子目录包。
 */
#include "Store.h"
#include "DataBase.h"
#include "FatFile.h"
#include "PhysicalMemory.h"
#include "HalSerial.h"
#include "SerialConfig.h"

#define STORE_COPY_MAX 4096u

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

static char CharacterToUpper(char C) {
    if (C >= 'a' && C <= 'z') {
        return (char)(C - 'a' + 'A');
    }
    return C;
}

static int ArchitectureMatches(const char *Arch) {
    if (Arch == 0 || Arch[0] == 0 || StringsEqual(Arch, "any") || StringsEqual(Arch, "-")) {
        return 1;
    }
    return StringsEqual(Arch, StoreHostArch());
}

/*
 * MakeInstallName — A + Id 大写截断 7 + 源扩展名
 * 谁调用：StoreInstall。
 */
static int MakeInstallName(char *Out, int Capacity, const char *Id, const char *File) {
    int o = 0;
    int i;
    const char *Dot;

    if (Out == 0 || Capacity < 8 || Id == 0 || File == 0) {
        return STORE_INVAL;
    }
    Out[o++] = 'A';
    for (i = 0; Id[i] && o < 8; i++) {
        char C = CharacterToUpper(Id[i]);
        if ((C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9')) {
            Out[o++] = C;
        }
    }
    Dot = File;
    while (*Dot && *Dot != '.') {
        Dot++;
    }
    if (*Dot == '.') {
        int e = 0;
        Out[o++] = '.';
        Dot++;
        while (*Dot && e < 3 && o + 1 < Capacity) {
            Out[o++] = CharacterToUpper(*Dot++);
            e++;
        }
    }
    Out[o] = 0;
    return STORE_OK;
}

static int MarkInstalled(const char *Id, const char *Type, const char *Destination) {
    char Key[DATA_BASE_KEY_MAX];
    char Value[DATA_BASE_VALUE_MAX];
    int k = 0;
    int v = 0;
    int i;

    if (DataBaseInitialize() != DATA_BASE_OK) {
        return STORE_ERR;
    }
    Key[k++] = 's';
    Key[k++] = 'i';
    Key[k++] = '.';
    for (i = 0; Id[i] && k + 1 < DATA_BASE_KEY_MAX; i++) {
        Key[k++] = Id[i];
    }
    Key[k] = 0;
    for (i = 0; Type[i] && v + 1 < DATA_BASE_VALUE_MAX; i++) {
        Value[v++] = Type[i];
    }
    if (v + 1 < DATA_BASE_VALUE_MAX) {
        Value[v++] = '|';
    }
    for (i = 0; Destination[i] && v + 1 < DATA_BASE_VALUE_MAX; i++) {
        Value[v++] = Destination[i];
    }
    Value[v] = 0;
    if (DataBaseSet(Key, Value) != DATA_BASE_OK) {
        return STORE_ERR;
    }
    return STORE_OK;
}

/*
 * StoreInstall — 按 catalog id 拷贝包文件到根目录 A* 名
 *
 * 做什么：LoadCatalog → 读 File → 写 Destination → DataBase si.<id>。
 * 谁调用：StoreJobStep；将来同步 install 也可直调。
 * 前后文：前 — StoreLoadCatalog；兄弟 — FatFileReadPath/WritePath。
 * 返回：STORE_* 
 */
int StoreInstall(const char *Id) {
    STORE_ENTRY Tab[STORE_ENTRIES_MAX];
    int Count = 0;
    int i;
    int Found = -1;
    char Destination[STORE_FILE_MAX + 4];
    UINT8 *Buffer;
    UINT32 Pages;
    UINT32 Size = 0;
    int Result;

    if (Id == 0 || Id[0] == 0) {
        return STORE_INVAL;
    }
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) != STORE_OK) {
        return STORE_ERR;
    }
    for (i = 0; i < Count; i++) {
        if (StringsEqual(Tab[i].Id, Id)) {
            Found = i;
            break;
        }
    }
    if (Found < 0) {
        return STORE_NOENT;
    }
    if (!ArchitectureMatches(Tab[Found].Arch)) {
        HalSerialWriteChannel(SLOG_FS, "Store: arch mismatch\n");
        return STORE_INVAL;
    }
    if (MakeInstallName(Destination, (int)sizeof(Destination), Tab[Found].Id,
                        Tab[Found].File) != STORE_OK) {
        return STORE_INVAL;
    }
    Pages = (STORE_COPY_MAX + 4095u) / 4096u;
    Buffer = (UINT8 *)PhysicalMemoryAllocatePages(Pages);
    if (Buffer == 0) {
        return STORE_NOSPC;
    }
    Result = FatFileReadPath(Tab[Found].File, Buffer, STORE_COPY_MAX, &Size);
    if (Result < 0 || Size == 0) {
        PhysicalMemoryFreePages(Buffer, Pages);
        HalSerialWriteChannel(SLOG_FS, "Store: src missing\n");
        return STORE_NOENT;
    }
    if (Size > STORE_COPY_MAX) {
        PhysicalMemoryFreePages(Buffer, Pages);
        return STORE_NOSPC;
    }
    if (FatFileWritePath(Destination, Buffer, Size) != 0) {
        PhysicalMemoryFreePages(Buffer, Pages);
        HalSerialWriteChannel(SLOG_FS, "Store: write fail\n");
        return STORE_ERR;
    }
    PhysicalMemoryFreePages(Buffer, Pages);
    if (MarkInstalled(Tab[Found].Id, Tab[Found].Type, Destination) != STORE_OK) {
        HalSerialWriteChannel(SLOG_FS, "Store: db mark fail\n");
        return STORE_ERR;
    }
    HalSerialWriteChannel(SLOG_FS, "Store: install ok\n");
    return STORE_OK;
}
