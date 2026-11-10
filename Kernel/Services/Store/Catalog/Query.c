/*
 * Query.c — 已装清单（PR-NORM-16；IsInstalled/Flags/Depends 外搬）
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Db.h"

typedef struct {
    STORE_INSTALLED *Out;
    int Max;
    int Count;
} STORE_LIST_CTX;

static int ListInstalledCb(const char *Key, const char *Value, void *Ctx) {
    STORE_LIST_CTX *C = (STORE_LIST_CTX *)Ctx;
    STORE_INSTALLED *E;
    const char *Bar;
    int i;

    if (!Key || Key[0] != 's' || Key[1] != 'i' || Key[2] != '.') {
        return 0;
    }
    if (!C || !C->Out || C->Count >= C->Max) {
        return 1;
    }
    E = &C->Out[C->Count];
    for (i = 0; Key[3 + i] && i < STORE_ID_MAX - 1; i++) {
        E->Id[i] = Key[3 + i];
    }
    E->Id[i] = 0;
    E->Type[0] = 0;
    E->File[0] = 0;
    if (!Value) {
        C->Count++;
        return 0;
    }
    Bar = Value;
    while (*Bar && *Bar != '|') {
        Bar++;
    }
    {
        int n = 0;
        while (Value + n < Bar && n < (int)sizeof(E->Type) - 1) {
            E->Type[n] = Value[n];
            n++;
        }
        E->Type[n] = 0;
    }
    if (*Bar == '|') {
        Bar++;
        for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
            E->File[i] = Bar[i];
        }
        E->File[i] = 0;
    }
    C->Count++;
    return 0;
}

/* 镜像预置 Apps/<id>/ELF 但 BLOCKS.DB 漏了 si.* 时，菜单/桌面看不见。补登记。 */
static void StoreAdoptDiskApps(void) {
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;

    Tab = gStoreTab;
    if (StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count) < 0 || Count <= 0) {
        return;
    }
    for (i = 0; i < Count; i++) {
        if (EntryKind(Tab[i].Type) != STORE_KIND_APP) {
            continue;
        }
        if (StoreHasSi(Tab[i].Id)) {
            continue;
        }
        if (!StoreAppElfExists(Tab[i].Id, Tab[i].File)) {
            continue;
        }
        (void)StoreAdoptInstalled(Tab[i].Id);
    }
}

int StoreListInstalled(STORE_INSTALLED *Out, int Max, int *OutCount) {
    STORE_LIST_CTX Ctx;
    int i;
    int Keep;

    if (!Out || Max <= 0 || !OutCount) {
        return FAT_ERR_INVAL;
    }
    StoreAdoptDiskApps();
    Ctx.Out = Out;
    Ctx.Max = Max;
    Ctx.Count = 0;
    (void)DbForEach(ListInstalledCb, &Ctx);
    /*
     * 孤儿 si（DB 有、Apps 无）：菜单仍会灰显并回落商店图标。
     * BLOCKS: 曾误指 NVMe 时 Adopt 写过 U 盘 DB；与 StoreIsInstalled 对齐清掉。
     */
    Keep = 0;
    for (i = 0; i < Ctx.Count; i++) {
        STORE_INSTALLED *E = &Out[i];
        int IsApp = (E->Type[0] == 'a' && E->Type[1] == 'p' && E->Type[2] == 'p' &&
                     E->Type[3] == 0);

        if (IsApp && E->File[0] && !StoreAppElfExists(E->Id, E->File)) {
            ClearSiOrphan(E->Id);
            continue;
        }
        if (Keep != i) {
            Out[Keep] = *E;
        }
        Keep++;
    }
    Ctx.Count = Keep;
    *OutCount = Ctx.Count;
    return FAT_OK;
}
