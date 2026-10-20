/*
 * Catalog.c — 商店 catalog 门面（story-s4 / PR-S* / PR-M*）
 *
 * 离线 catalog；font/asset→Assets/；Db 已装清单 + remove；
 * depends=（catalog / PKG.TXT）；combo/uncombo 按依赖序。
 * 载荷：Store/packages/<id>/ → Store/ → 卷根；皆无则 HTTP 直装目标。
 * 卸装删 Apps|Fonts|Packs 副本。sha256=- 跳校验。
 * Load/Install/Combo/Remove/Query/Managed/Depends/Database 分文件。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "Desktop.h"
#include "Locale.h"
#include "FileSystem.h"
#include "Fat.h"
#include "Gui.h"
#include "Font.h"
#include "Theme.h"
#include "Scheduler.h"

/* 内核任务栈仅 8KiB；catalog 表放 BSS，避免 store sync/HTTP 栈溢出闪退 */
STORE_ENTRY gStoreTab[STORE_ENTRIES_MAX];
/* combo 嵌套卸装时合并 FontReload，避免连删字体卡死/重入 */
int gStoreComboDepth;
int gNeedFontReload;

/*
 * 长 IO 呼吸：薄包装 → SchedulerIoBreath（PR-K-preempt-breath）。
 * FatSetIoBreath(StoreIoBreath) 仍有效；勿删本符号。
 */
void StoreMenuRegister(void) {
    DesktopMenuRegister(DESKTOP_ACTION_STORE, MSG_ICON_STORE, "Store", 3, 1);
}

void StoreIoBreath(void) {
    SchedulerIoBreath();
}

void StoreFlushFontReload(void) {
    if (!gNeedFontReload) {
        return;
    }
    gNeedFontReload = 0;
    StoreIoBreath();
    (void)FontReloadAssets();
    ThemeClampFontId();
    /* 字高变了须重合成，否则桌面仍按旧度量画 */
    GuiComposeThemeScene();
    /* PR-S-compose-sep（序 4）：合成后、写盘前呼吸一次，避免 compose+ThemeSave
     * 连续长消费段冻住光标；ThemeSave 内 DB 写盘走 FatSetIoBreath 持续呼吸 */
    StoreIoBreath();
    FatSetIoBreath(StoreIoBreath);
    (void)ThemeSave();
    FatSetIoBreath(0);
    StoreIoBreath();
}

STORE_ENTRY *StoreScratchTab(void) {
    return gStoreTab;
}


/* FAT 上文件名大小写不一（HELLO.ELF vs hello.elf） */
int DirHasFileCI(const char *Dir, const char *File) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    int N = 0;
    int i;

    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, File)) {
            return 1;
        }
    }
    return 0;
}

/* 返回目录里真实名（大小写以盘为准），供 Delete 路径 */
int DirResolveFileCI(const char *Dir, const char *File, char *Out, int OutMax) {
    static FAT_DIRECTORY_ENTRY Ents[FAT_LIST_MAX];
    char Want[STORE_FILE_MAX];
    int N = 0;
    int i;
    int w;

    if (!Out || OutMax <= 0) {
        return 0;
    }
    Out[0] = 0;
    if (!Dir || !File || File[0] == 0) {
        return 0;
    }
    /* File 与 Out 可能同缓冲：先拷走 */
    w = 0;
    while (File[w] && w + 1 < (int)sizeof(Want)) {
        Want[w] = File[w];
        w++;
    }
    Want[w] = 0;
    if (Want[0] == 0) {
        return 0;
    }
    if (FileSystemListEntries(Dir, Ents, FAT_LIST_MAX, &N) != FAT_OK || N <= 0) {
        return 0;
    }
    for (i = 0; i < N; i++) {
        if (Ents[i].Attr & FAT_ATTR_DIR) {
            continue;
        }
        if (StrEqIgnoreCase(Ents[i].Name, Want)) {
            int k = 0;
            while (Ents[i].Name[k] && k + 1 < OutMax) {
                Out[k] = Ents[i].Name[k];
                k++;
            }
            Out[k] = 0;
            return Out[0] != 0;
        }
    }
    return 0;
}


/* 规范化：空 / "-" → 空串（表示无依赖） */
void NormalizeDepends(char *Dep) {
    if (!Dep) {
        return;
    }
    if (Dep[0] == 0 || (Dep[0] == '-' && Dep[1] == 0)) {
        Dep[0] = 0;
    }
}

int ArchOk(const char *Arch) {
    const char *Host;

    if (!Arch || Arch[0] == 0 || StringEqual(Arch, "any")) {
        return 1;
    }
    Host = StoreHostArch();
    return StringEqual(Arch, Host);
}

static int EnsureDir(const char *Path) {
    int Err = FileSystemMakeDirectory(Path);
    if (Err == FAT_OK || Err == FAT_ERR_EXIST) {
        return FAT_OK;
    }
    return Err;
}

int EnsureAppsDir(void) {
    return EnsureDir(STORE_APPS_DIR);
}

int EnsureFontsDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_FONTS_DIR);
}

int EnsurePacksDir(void) {
    int Err = EnsureDir("Assets");
    if (Err != FAT_OK) {
        return Err;
    }
    return EnsureDir(STORE_PACKS_DIR);
}

int EntryKind(const char *Type) {
    if (StringEqual(Type, "font")) {
        return STORE_KIND_FONT;
    }
    if (StringEqual(Type, "asset")) {
        return STORE_KIND_ASSET;
    }
    if (StringEqual(Type, "lib") || StringEqual(Type, "library")) {
        return STORE_KIND_LIB;
    }
    if (StringEqual(Type, "app") || Type[0] == 0) {
        return STORE_KIND_APP;
    }
    return -1;
}
