/*
 * ShellCommandsFsUiStoreJob.c — PR-S3-shellfsui-1：store Job/列表帮手
 */
#include "ShellPrivate.h"
#include "Console.h"
#include "Store.h"
#include "StoreJob.h"
#include "Fat.h"
#include "Hal.h"
#include "HalDevices.h"

int ShellStoreJob(STORE_JOB_KIND Kind, const char *Id) {
    gConsole.JobHoldPrompt();
    if (StoreJobShellRun(Kind, Id) != 0) {
        gConsole.Write("store: busy (Cancel in Store UI, or store job)\n");
        gConsole.JobReleasePrompt();
        return -1;
    }
    return 0;
}

void ShellStoreQueued(const char *Verb, const char *Id) {
    gConsole.Write("store: queued ");
    if (Verb && Verb[0]) {
        gConsole.Write(Verb);
        gConsole.Write(" ");
    }
    if (Id && Id[0]) {
        gConsole.Write(Id);
    }
    gConsole.Write("\n");
    gConsole.Write("hint: worker runs it; open another shell if needed\n");
}

void ShellStoreNetDone(const char *Verb, int Err) {
    if (Err == 0) {
        gConsole.Write("store: ");
        gConsole.Write(Verb);
        gConsole.Write(" ok\n");
        return;
    }
    gConsole.Write("store: ");
    gConsole.Write(Verb);
    gConsole.Write(" fail\n");
    if (Err == -41) {
        gConsole.Write("hint: HTTP not 200\n");
    } else if (Err == -42) {
        gConsole.Write("hint: hash mismatch\n");
    } else if (Err == -40) {
        gConsole.Write("hint: net/tcp fail\n");
    } else if (Err == -43) {
        gConsole.Write("hint: out of memory\n");
    }
}

void ShellStoreJobStatus(void) {
    char Buf[96];
    int Err;
    int i;

    StoreJobShellPumpBegin();
    for (i = 0; i < 16 && StoreJobUiIsBusy(); i++) {
        if (StoreJobStep() != 0) {
            break;
        }
    }
    StoreJobShellPumpEnd();

    if (StoreJobUiIsBusy()) {
        StoreJobGetStatus(Buf, (int)sizeof(Buf));
        gConsole.Write("store job: busy");
        if (Buf[0]) {
            gConsole.Write("  ");
            gConsole.Write(Buf);
        }
        gConsole.Write("\n");
        return;
    }
    Err = StoreJobLastError();
    gConsole.Write("store job: idle  last=");
    if (Err == FAT_OK || Err == 0) {
        gConsole.Write("ok");
    } else if (Err == STORE_JOB_ERR_CANCEL) {
        gConsole.Write("cancelled");
    } else {
        gConsole.Write(FatStrError(Err));
    }
    gConsole.Write("\n");
}

int StoreWordEq(const char *A, const char *B) {
    if (A == 0 || B == 0) {
        return 0;
    }
    while (*A && *B) {
        if (*A != *B) {
            return 0;
        }
        A++;
        B++;
    }
    return *A == *B;
}

void StorePrintUsage(void) {
    gConsole.Write(
        "usage: store <list|install|remove|combo|uncombo|installed|sync|repo|job> ...\n");
    gConsole.Write(
        "  aliases: (none)→list, status→list, rm→remove, list-installed→installed\n");
}

void StoreCmdListCatalog(void) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Err;

    Tab = StoreScratchTab();
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0) {
        gConsole.Write("store: catalog ");
        gConsole.Write(FatStrError(Err));
        gConsole.Write("\n");
        return;
    }
    gConsole.Write("store (");
    gConsole.Write(StoreHostArch());
    gConsole.Write(")  *=[本|网] id …\n");
    for (i = 0; i < Count; i++) {
        char Dep[64];
        int On = StoreIsInstalled(Tab[i].Id);
        if (On) {
            (void)StoreGetDepends(Tab[i].Id, Dep, (int)sizeof(Dep));
        } else if (Tab[i].Depends[0]) {
            int k = 0;
            while (Tab[i].Depends[k] && k < (int)sizeof(Dep) - 1) {
                Dep[k] = Tab[i].Depends[k];
                k++;
            }
            Dep[k] = 0;
        } else {
            Dep[0] = '-';
            Dep[1] = 0;
        }
        gConsole.Write(On ? "* " : "  ");
        gConsole.Write(Tab[i].Origin == STORE_SRC_NET ? "[网] " : "[本] ");
        gConsole.Write(Tab[i].Id);
        gConsole.Write("  ");
        gConsole.Write(Tab[i].Type);
        gConsole.Write("  dep=");
        gConsole.Write(Dep);
        gConsole.Write("  ");
        gConsole.Write(Tab[i].File);
        gConsole.Write("  ");
        gConsole.Write(Tab[i].Title);
        gConsole.Write("\n");
    }
}
