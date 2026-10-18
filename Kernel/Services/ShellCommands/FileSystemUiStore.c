/*
 * ShellCommandsFsUiStore.c — PR-S3-shellfsui-1：CommandStore + 注册
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Store.h"
#include "StoreJob.h"
#include "Fat.h"
#include "Hal.h"
#include "LwIp.h"
#include "HalDevices.h"

static void CommandStore(int Argc, char **Argv) {
    const char *Sub;
    int i;
    int Err;
    UINT32 Ip;
    UINT16 Port;
    char IpBuf[24];

    /* PR-C3：正统二级无中横线；别名在此展开 */
    if (Argc < 2) {
        Sub = "list";
    } else {
        Sub = Argv[1];
        if (StoreWordEq(Sub, "status")) {
            Sub = "list";
        } else if (StoreWordEq(Sub, "job")) {
            ShellStoreJobStatus();
            return;
        } else if (StoreWordEq(Sub, "rm")) {
            Sub = "remove";
        } else if (StoreWordEq(Sub, "list-installed")) {
            Sub = "installed";
        }
    }

    if (StoreWordEq(Sub, "repo")) {
        if (Argc >= 3) {
            if (StoreRepoSet(Argv[2]) != 0) {
                gConsole.Write("store repo: bad ip:port\n");
                return;
            }
            gConsole.Write("store: repo set\n");
            return;
        }
        StoreRepoGet(&Ip, &Port);
        HalNetFormatIp(Ip, IpBuf, (int)sizeof(IpBuf));
        gConsole.Write("store repo ");
        gConsole.Write(IpBuf);
        gConsole.Write(":");
        gConsole.WriteHex32(Port);
        gConsole.Write("\n");
        return;
    }

    if (StoreWordEq(Sub, "sync")) {
        if (LwIpActive()) {
            int Err;

            if (StoreJobShellBegin() != 0) {
                gConsole.Write("store: busy (Cancel in Store UI, or store job)\n");
                return;
            }
            gConsole.Write("store: syncing catalog...\n");
            Err = StoreSyncCatalog();
            StoreJobShellEnd();
            ShellStoreNetDone("sync", Err);
            return;
        }
        if (ShellStoreJob(STORE_JOB_SYNC, 0) != 0) {
            return;
        }
        ShellStoreQueued("sync", 0);
        return;
    }

    if (StoreWordEq(Sub, "fetch")) {
        if (Argc < 3) {
            gConsole.Write("usage: store fetch <id>\n");
            return;
        }
        if (LwIpActive()) {
            int Err;

            if (StoreJobShellBegin() != 0) {
                gConsole.Write("store: busy (Cancel in Store UI, or store job)\n");
                return;
            }
            gConsole.Write("store: fetching ");
            gConsole.Write(Argv[2]);
            gConsole.Write("...\n");
            Err = StoreFetchId(Argv[2]);
            StoreJobShellEnd();
            ShellStoreNetDone("fetch", Err);
            return;
        }
        if (ShellStoreJob(STORE_JOB_FETCH, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued("fetch", Argv[2]);
        return;
    }

    if (StoreWordEq(Sub, "install") || StoreWordEq(Sub, "combo")) {
        if (Argc < 3) {
            if (StoreWordEq(Sub, "combo")) {
                gConsole.Write("usage: store combo <id>\n");
                gConsole.Write("hint: e.g. store combo guidemo  (demopack+sun8 then app)\n");
            } else {
                gConsole.Write("usage: store install <id>\n");
            }
            return;
        }
        if (ShellStoreJob(STORE_JOB_INSTALL, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued(StoreWordEq(Sub, "combo") ? "combo" : "install", Argv[2]);
        return;
    }

    if (StoreWordEq(Sub, "installed")) {
        STORE_INSTALLED Inst[STORE_INSTALLED_MAX];
        int N = 0;
        Err = StoreListInstalled(Inst, STORE_INSTALLED_MAX, &N);
        if (Err != FAT_OK) {
            gConsole.Write("store installed: ");
            gConsole.Write(FatStrError(Err));
            gConsole.Write("\n");
            return;
        }
        gConsole.Write("store installed:\n");
        if (N == 0) {
            gConsole.Write("  (none)\n");
            return;
        }
        for (i = 0; i < N; i++) {
            char Dep[64];
            gConsole.Write("  ");
            gConsole.Write(Inst[i].Id);
            gConsole.Write("  ");
            gConsole.Write(Inst[i].Type);
            gConsole.Write("  ");
            gConsole.Write(Inst[i].File);
            gConsole.Write("  dep=");
            (void)StoreGetDepends(Inst[i].Id, Dep, (int)sizeof(Dep));
            gConsole.Write(Dep);
            gConsole.Write("\n");
        }
        return;
    }

    if (StoreWordEq(Sub, "remove") || StoreWordEq(Sub, "uncombo")) {
        if (Argc < 3) {
            gConsole.Write(StoreWordEq(Sub, "uncombo")
                             ? "usage: store uncombo <id>\n"
                             : "usage: store remove <id>\n");
            return;
        }
        if (ShellStoreJob(STORE_JOB_REMOVE, Argv[2]) != 0) {
            return;
        }
        ShellStoreQueued(StoreWordEq(Sub, "uncombo") ? "uncombo" : "remove", Argv[2]);
        return;
    }

    if (StoreWordEq(Sub, "list")) {
        StoreCmdListCatalog();
        return;
    }

    StorePrintUsage();
}

void ShellCommandsFsUiStoreRegister(void) {
    gConsole.Register("store", "store list|install|remove|combo|uncombo|installed|…", CommandStore);
}
