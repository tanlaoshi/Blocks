/*
 * QueryInstalled.c — StoreIsInstalled（PR-NORM-16）
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Db.h"

int StoreIsInstalled(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    const char *Bar;
    const char *Dir;
    STORE_ENTRY *Tab;
    int Count = 0;
    int i;
    int Kind;
    int Err;

    if (!Id || Id[0] == 0) {
        return 0;
    }
    if (!MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        return 0;
    }
    if (DbGet(Key, Val, sizeof(Val)) == DB_OK) {
        /* si.* 须对应盘上文件；否则清孤儿清单（Files 曾直删时） */
        Bar = Val;
        i = 0;
        while (Val[i] && Val[i] != '|' && i < (int)sizeof(Type) - 1) {
            Type[i] = Val[i];
            i++;
        }
        Type[i] = 0;
        File[0] = 0;
        while (*Bar && *Bar != '|') {
            Bar++;
        }
        if (*Bar == '|') {
            Bar++;
            for (i = 0; Bar[i] && i < STORE_FILE_MAX - 1; i++) {
                File[i] = Bar[i];
            }
            File[i] = 0;
        }
        Kind = EntryKind(Type);
        if (Kind == STORE_KIND_FONT) {
            Dir = STORE_FONTS_DIR;
        } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            Dir = STORE_PACKS_DIR;
        } else if (Kind == STORE_KIND_APP) {
            if (File[0] && StoreAppElfExists(Id, File)) {
                return 1;
            }
            (void)DbDelete(Key);
            if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
                (void)DbDelete(DepKey);
            }
            return 0;
        } else {
            Dir = STORE_APPS_DIR;
        }
        if (File[0] && DirHasFileCI(Dir, File)) {
            return 1;
        }
        (void)DbDelete(Key);
        if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
            (void)DbDelete(DepKey);
        }
        return 0;
    }
    /*
     * 镜像预置（Apps/HELLO.ELF 等）无 si.* 时：按 catalog 落盘路径探测。
     * 开始菜单扫目录能看到，商店也应标 installed。
     */
    Tab = gStoreTab;
    Err = StoreLoadCatalog(Tab, STORE_ENTRIES_MAX, &Count);
    if (Err < 0 || Count <= 0) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        if (!StringEqual(Tab[i].Id, Id)) {
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            return StoreAppElfExists(Tab[i].Id, Tab[i].File);
        }
        if (Kind == STORE_KIND_FONT) {
            return DirHasFileCI(STORE_FONTS_DIR, Tab[i].File);
        }
        if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            return DirHasFileCI(STORE_PACKS_DIR, Tab[i].File);
        }
        return 0;
    }
    return 0;
}
