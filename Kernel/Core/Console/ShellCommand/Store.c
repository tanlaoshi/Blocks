/*
 * Store.c — K47/K48：store list / store install（经 StoreJob）
 *
 * 【初学者】
 * Shell 路径：入队后本命令内 Step 完成（串口一次见 ok）。
 * 若 Store 窗已占忙旗，则 store: busy。
 */
#include "ShellCommand.h"
#include "Store.h"
#include "StoreJob.h"
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
        Error = StoreJobInstall(Argv[2]);
        if (Error == STORE_BUSY) {
            Put("store: busy\n");
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
        (void)StoreJobStep();
        Error = StoreJobLastError();
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

/*
 * StoreRegister — 注册 store list|install
 *
 * 谁调用：ShellCommandInitialize。
 * 返回：void
 */
void StoreRegister(void) {
    ShellCommandRegister("store", "list/install local packages", CommandStore);
}
