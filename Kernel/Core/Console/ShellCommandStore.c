/*
 * ShellCommandStore.c — K47：store list / store install（不堆进 ShellCommand.c）
 *
 * 【初学者】
 * 读清单与装包走 Store*；本文件只做参数与串口提示。
 */
#include "ShellCommand.h"
#include "Store.h"
#include "HalSerial.h"

static void Put(const char *S) {
    HalSerialWriteShell(S);
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

static void PutUnsigned32(UINT32 V) {
    char B[12];
    int i = 0;
    int j;
    char T;
    if (V == 0) {
        Put("0");
        return;
    }
    while (V > 0 && i < 11) {
        B[i++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    for (j = 0; j < i / 2; j++) {
        T = B[j];
        B[j] = B[i - 1 - j];
        B[i - 1 - j] = T;
    }
    B[i] = 0;
    Put(B);
}

/*
 * CommandStore — store list | store install <id>
 * 谁调用：Shell 行。
 * 前后文：后 — 根目录安装名 + BLOCKS.DB si.*
 */
static void CommandStore(int Argc, char **Argv) {
    STORE_ENTRY Tab[STORE_ENTRIES_MAX];
    int Count = 0;
    int i;
    int Error;

    if (Argc < 2) {
        Put("usage: store list | store install <id>\n");
        return;
    }
    if (Argc == 2 && StringsEqual(Argv[1], "list")) {
        Error = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
        if (Error != STORE_OK) {
            Put("store: catalog fail\n");
            return;
        }
        Put("store: ");
        PutUnsigned32((UINT32)Count);
        Put(" pkg(s) arch=");
        Put(StoreHostArch());
        Put("\n");
        for (i = 0; i < Count; i++) {
            Put("  ");
            Put(Tab[i].Id);
            Put("  ");
            Put(Tab[i].Type);
            Put("  ");
            Put(Tab[i].File);
            Put("  ");
            Put(Tab[i].Title);
            Put("\n");
        }
        return;
    }
    if (Argc >= 3 && StringsEqual(Argv[1], "install")) {
        Error = StoreInstall(Argv[2]);
        if (Error == STORE_NOENT) {
            Put("store: not found\n");
            return;
        }
        if (Error == STORE_INVAL) {
            Put("store: invalid\n");
            return;
        }
        if (Error != STORE_OK) {
            Put("store: fail\n");
            return;
        }
        Put("store: ok ");
        Put(Argv[2]);
        Put("\n");
        return;
    }
    Put("usage: store list | store install <id>\n");
}

void ShellCommandStoreRegister(void) {
    ShellCommandRegister("store", "list/install local packages", CommandStore);
}
