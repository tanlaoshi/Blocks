/*
 * AppFont.c — PR-S-app-font：exec 时按 PKG font= 挂/卸应用私有字
 *
 * 单用户 GUI 下切全局当前字即可；无 font= 或非 Apps/<id>/ 路径则保持系统字。
 */
#include "ProcessPrivate.h"
#include "Store.h"
#include "StorePrivate.h"
#include "Font.h"
#include "Fat.h"
#include "Debug.h"

static UINT32 gSavedFontId;
static int gAppFontHeld;

static int EqCiPrefix(const char *S, const char *Pfx) {
    while (*Pfx) {
        char A = *S;
        char B = *Pfx;

        if (A >= 'A' && A <= 'Z') {
            A = (char)(A - 'A' + 'a');
        }
        if (B >= 'A' && B <= 'Z') {
            B = (char)(B - 'A' + 'a');
        }
        if (A != B) {
            return 0;
        }
        S++;
        Pfx++;
    }
    return 1;
}

/* 路径末段 → 小写 Id；剥 .ELF；失败 -1 */
static int IdFromElfBasename(const char *Path, char *Id, int IdMax) {
    const char *Base = Path;
    const char *P;
    int i = 0;

    if (!Path || !Path[0] || !Id || IdMax < 2) {
        return -1;
    }
    for (P = Path; *P; P++) {
        if (*P == '/' || *P == '\\') {
            Base = P + 1;
        }
    }
    while (*Base && *Base != '.' && i + 1 < IdMax) {
        char C = *Base++;

        if (C >= 'A' && C <= 'Z') {
            C = (char)(C - 'A' + 'a');
        }
        Id[i++] = C;
    }
    Id[i] = 0;
    return i > 0 ? 0 : -1;
}

/*
 * Path → 包 Id：
 *   Apps/<id>/... → id
 *   Class/NAME.ELF / NAME.ELF → lower(NAME)（课堂 Class/ 与 PKG window= 对齐）
 * 失败 -1
 */
static int ExtractAppsId(const char *Path, char *Id, int IdMax) {
    const char *P = Path;
    int i;

    if (!Path || !Id || IdMax < 2) {
        return -1;
    }
    while (*P == '/' || *P == '\\') {
        P++;
    }
    if (EqCiPrefix(P, "Apps/")) {
        P += 5;
        i = 0;
        while (*P && *P != '/' && *P != '\\' && i + 1 < IdMax) {
            Id[i++] = *P++;
        }
        Id[i] = 0;
        if (i == 0 || (*P != '/' && *P != '\\')) {
            return -1;
        }
        return 0;
    }
    /* Class/<ELF> 或裸 ELF：按基名对 PKG（SNAKE→snake） */
    if (EqCiPrefix(P, "Class/")) {
        P += 6;
    }
    return IdFromElfBasename(P, Id, IdMax);
}

void ProcessRestoreAppFont(void) {
    if (!gAppFontHeld) {
        return;
    }
    FontUnloadApp();
    if (FontSetById(gSavedFontId) != 0 && FontCount() > 0) {
        (void)FontSetById(0);
    }
    gAppFontHeld = 0;
    DebugWrite("process: app font restored\n");
}

/* Class 开窗 Demo：PKG 读失败时仍勿 CoopDrain（空窗+鼠死） */
static int ClassElfTakesWindow(const char *Id) {
    static const char *const kWin[] = {
        "snake", "taskmgr", "chat", 0
    };
    int i;

    if (!Id || !Id[0]) {
        return 0;
    }
    for (i = 0; kWin[i]; i++) {
        const char *A = Id;
        const char *B = kWin[i];

        while (*A && *B && *A == *B) {
            A++;
            B++;
        }
        if (*A == 0 && *B == 0) {
            return 1;
        }
    }
    return 0;
}

int ProcessAppTakesWindow(const char *Path) {
    STORE_APP_DESKTOP_META Meta;
    char Id[STORE_ID_MAX];

    if (!Path || ExtractAppsId(Path, Id, (int)sizeof(Id)) != 0) {
        return 0;
    }
    if (StoreReadAppDesktopMeta(Id, &Meta) == FAT_OK && Meta.WindowYes) {
        return 1;
    }
    return ClassElfTakesWindow(Id);
}

void ProcessApplyAppFont(const char *Path) {
    STORE_APP_DESKTOP_META Meta;
    char Id[STORE_ID_MAX];
    char FontPath[128];
    int Fid;

    ProcessRestoreAppFont();
    if (!Path || ExtractAppsId(Path, Id, (int)sizeof(Id)) != 0) {
        return;
    }
    if (StoreReadAppDesktopMeta(Id, &Meta) != FAT_OK || !Meta.FontRel[0]) {
        return;
    }

    StoreAppBundleDir(FontPath, (int)sizeof(FontPath), Id);
    JoinPath(FontPath, (int)sizeof(FontPath), FontPath, Meta.FontRel);

    Fid = FontLoadPath(FontPath);
    if (Fid < 0) {
        DebugWrite("process: app font load fail ");
        DebugWrite(FontPath);
        DebugWrite("\n");
        return;
    }

    gSavedFontId = FontCurrentId();
    if (FontSetById((UINT32)Fid) != 0) {
        FontUnloadApp();
        DebugWrite("process: app font set fail\n");
        return;
    }
    gAppFontHeld = 1;
    DebugWrite("process: app font ");
    DebugWrite(FontPath);
    DebugWrite(" id=");
    DebugHex32((UINT32)Fid);
    DebugWrite("\n");
}
