/*
 * QueryInstalledFlags.c — 已装标志批量探测（PR-NORM-16）
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Db.h"

void ClearSiOrphan(const char *Id) {
    char Key[DB_KEY_MAX];
    char DepKey[DB_KEY_MAX];

    if (MakeDbKey(Key, (int)sizeof(Key), "si.", Id)) {
        (void)DbDelete(Key);
    }
    if (MakeDbKey(DepKey, (int)sizeof(DepKey), "sd.", Id)) {
        (void)DbDelete(DepKey);
    }
}

static int DirListOnce(const char *Dir, FAT_DIRECTORY_ENTRY *Ents, int *N) {
    if (*N >= 0) {
        return *N;
    }
    *N = 0;
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, N) != FAT_OK) {
        *N = 0;
    }
    StoreIoBreath();
    return *N;
}

static int DirListHasFile(const FAT_DIRECTORY_ENTRY *Ents, int N, const char *File) {
    int j;

    if (!Ents || !File || !File[0] || N <= 0) {
        return 0;
    }
    for (j = 0; j < N; j++) {
        if (!(Ents[j].Attr & FAT_ATTR_DIR) && StrEqIgnoreCase(Ents[j].Name, File)) {
            return 1;
        }
    }
    return 0;
}

static int AppElfPresent(const char *Id, const char *File, FAT_DIRECTORY_ENTRY *Apps,
                         int *Na) {
    char Path[160];
    FAT_FILE_STAT St;

    if (!File || !File[0]) {
        return 0;
    }
    if (Id && Id[0]) {
        StoreAppElfPath(Path, (int)sizeof(Path), Id, File);
        if (FileSystemFileStat(Path, &St) == FAT_OK && !(St.Attr & FAT_ATTR_DIR)) {
            return 1;
        }
    }
    (void)DirListOnce(STORE_APPS_DIR, Apps, Na);
    return DirListHasFile(Apps, *Na, File);
}

void StoreFillInstalledFlags(const STORE_ENTRY *Tab, int Count, int *OutFlags) {
    static FAT_DIRECTORY_ENTRY Fonts[FAT_LIST_MAX];
    static FAT_DIRECTORY_ENTRY Packs[FAT_LIST_MAX];
    static FAT_DIRECTORY_ENTRY Apps[FAT_LIST_MAX];
    char Key[DB_KEY_MAX];
    char Val[DB_VAL_MAX];
    char Type[12];
    char File[STORE_FILE_MAX];
    const char *Bar;
    int Nf = -1;
    int Np = -1;
    int Na = -1;
    int i;
    int t;
    int Kind;
    int Ok;

    if (!OutFlags) {
        return;
    }
    if (!Tab || Count <= 0) {
        return;
    }
    if (Count > STORE_ENTRIES_MAX) {
        Count = STORE_ENTRIES_MAX;
    }

    for (i = 0; i < Count; i++) {
        OutFlags[i] = 0;
        if (!Tab[i].Id[0]) {
            continue;
        }
        if (MakeDbKey(Key, (int)sizeof(Key), "si.", Tab[i].Id) &&
            DbGet(Key, Val, sizeof(Val)) == DB_OK) {
            /* 勿再调 StoreIsInstalled：会二次 DbGet，且无 si 时还整表 LoadCatalog */
            t = 0;
            while (Val[t] && Val[t] != '|' && t < (int)sizeof(Type) - 1) {
                Type[t] = Val[t];
                t++;
            }
            Type[t] = 0;
            File[0] = 0;
            Bar = Val;
            while (*Bar && *Bar != '|') {
                Bar++;
            }
            if (*Bar == '|') {
                Bar++;
                t = 0;
                while (Bar[t] && t < STORE_FILE_MAX - 1) {
                    File[t] = Bar[t];
                    t++;
                }
                File[t] = 0;
            }
            Kind = EntryKind(Type);
            Ok = 0;
            if (Kind == STORE_KIND_APP) {
                Ok = AppElfPresent(Tab[i].Id, File[0] ? File : Tab[i].File, Apps, &Na);
            } else if (Kind == STORE_KIND_FONT) {
                (void)DirListOnce(STORE_FONTS_DIR, Fonts, &Nf);
                Ok = DirListHasFile(Fonts, Nf, File[0] ? File : Tab[i].File);
            } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
                (void)DirListOnce(STORE_PACKS_DIR, Packs, &Np);
                Ok = DirListHasFile(Packs, Np, File[0] ? File : Tab[i].File);
            } else {
                (void)DirListOnce(STORE_APPS_DIR, Apps, &Na);
                Ok = DirListHasFile(Apps, Na, File[0] ? File : Tab[i].File);
            }
            if (Ok) {
                OutFlags[i] = 1;
            } else {
                ClearSiOrphan(Tab[i].Id);
            }
            continue;
        }
        Kind = EntryKind(Tab[i].Type);
        if (Kind == STORE_KIND_APP) {
            OutFlags[i] = AppElfPresent(Tab[i].Id, Tab[i].File, Apps, &Na) ? 1 : 0;
        } else if (Kind == STORE_KIND_FONT) {
            (void)DirListOnce(STORE_FONTS_DIR, Fonts, &Nf);
            OutFlags[i] = DirListHasFile(Fonts, Nf, Tab[i].File) ? 1 : 0;
        } else if (Kind == STORE_KIND_ASSET || Kind == STORE_KIND_LIB) {
            (void)DirListOnce(STORE_PACKS_DIR, Packs, &Np);
            OutFlags[i] = DirListHasFile(Packs, Np, Tab[i].File) ? 1 : 0;
        }
    }
}
