/*
 * StorePkgMeta.c — PR-S-bundle-desktop：读 Apps/<id>/PKG.TXT 桌面相关键
 *
 * Apps 包优先；缺键再合并 Store/packages/<id>/PKG.TXT。
 * 半截 Apps/PKG（无 desktop/taskbar）不再挡住商店包里的 yes → 装完菜单/桌面空。
 */
#include "Store.h"
#include "StorePrivate.h"
#include "FileSystem.h"
#include "Fat.h"

#define PKG_BUF_MAX 2048

typedef struct {
    STORE_APP_DESKTOP_META *Out;
    int FillMissingOnly;
    int SeenDesktop;
    int SeenTaskbar;
    int SeenWindow;
    int SeenTitle;
    int SeenIcon;
    int SeenFont;
    int SeenCategory;
} PKG_PARSE_STATE;

static int IsYesToken(const char *Val) {
    if (!Val || !Val[0]) {
        return 0;
    }
    if (Val[0] == '1' && Val[1] == 0) {
        return 1;
    }
    if ((Val[0] == 'y' || Val[0] == 'Y') &&
        (Val[1] == 'e' || Val[1] == 'E') &&
        (Val[2] == 's' || Val[2] == 'S') && Val[3] == 0) {
        return 1;
    }
    return 0;
}

static void ApplyPkgLine(PKG_PARSE_STATE *State, const char *Key, const char *Val) {
    STORE_APP_DESKTOP_META *Out;

    if (!State || !State->Out || !Key) {
        return;
    }
    Out = State->Out;
    if (StrEqIgnoreCase(Key, "desktop")) {
        if (State->FillMissingOnly && State->SeenDesktop) {
            return;
        }
        Out->DesktopYes = IsYesToken(Val);
        State->SeenDesktop = 1;
    } else if (StrEqIgnoreCase(Key, "window")) {
        if (State->FillMissingOnly && State->SeenWindow) {
            return;
        }
        Out->WindowYes = IsYesToken(Val);
        State->SeenWindow = 1;
    } else if (StrEqIgnoreCase(Key, "taskbar")) {
        if (State->FillMissingOnly && State->SeenTaskbar) {
            return;
        }
        Out->TaskbarYes = IsYesToken(Val);
        State->SeenTaskbar = 1;
    } else if (StrEqIgnoreCase(Key, "title") && Val) {
        if (State->FillMissingOnly && State->SeenTitle) {
            return;
        }
        StringCopy(Out->Title, (int)sizeof(Out->Title), Val);
        State->SeenTitle = 1;
    } else if (StrEqIgnoreCase(Key, "icon") && Val) {
        if (State->FillMissingOnly && State->SeenIcon) {
            return;
        }
        StringCopy(Out->IconRel, (int)sizeof(Out->IconRel), Val);
        State->SeenIcon = 1;
    } else if (StrEqIgnoreCase(Key, "font") && Val) {
        if (State->FillMissingOnly && State->SeenFont) {
            return;
        }
        StringCopy(Out->FontRel, (int)sizeof(Out->FontRel), Val);
        State->SeenFont = 1;
    } else if (StrEqIgnoreCase(Key, "category") && Val) {
        if (State->FillMissingOnly && State->SeenCategory) {
            return;
        }
        StringCopy(Out->Category, (int)sizeof(Out->Category), Val);
        Out->CategoryGame = StrEqIgnoreCase(Val, "game") ? 1 : 0;
        State->SeenCategory = 1;
    }
}

static void ParsePackageBuffer(const char *Buf, UINTN Size, PKG_PARSE_STATE *State) {
    UINTN i = 0;

    while (i < Size) {
        char Key[32];
        char Val[96];
        int Ki = 0;
        int Vi = 0;

        while (i < Size && (Buf[i] == '\r' || Buf[i] == '\n' || Buf[i] == ' ')) {
            i++;
        }
        if (i >= Size) {
            break;
        }
        if (Buf[i] == '#') {
            while (i < Size && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        while (i < Size && Buf[i] != '=' && Buf[i] != '\n' && Ki + 1 < (int)sizeof(Key)) {
            if (Buf[i] != '\r') {
                Key[Ki++] = Buf[i];
            }
            i++;
        }
        Key[Ki] = 0;
        if (i >= Size || Buf[i] != '=') {
            while (i < Size && Buf[i] != '\n') {
                i++;
            }
            continue;
        }
        i++;
        while (i < Size && Buf[i] != '\n' && Vi + 1 < (int)sizeof(Val)) {
            if (Buf[i] != '\r') {
                Val[Vi++] = Buf[i];
            }
            i++;
        }
        Val[Vi] = 0;
        while (Vi > 0 && (Val[Vi - 1] == ' ' || Val[Vi - 1] == '\t')) {
            Val[--Vi] = 0;
        }
        ApplyPkgLine(State, Key, Val);
        if (i < Size && Buf[i] == '\n') {
            i++;
        }
    }
}

static int ReadPkgFile(const char *Path, char *Buf, UINTN BufMax, UINTN *OutSize) {
    UINTN Size = 0;
    int Err;

    if (!Path || !Buf || BufMax < 2 || !OutSize) {
        return FAT_ERR_INVAL;
    }
    Err = FileSystemReadFile(Path, Buf, BufMax - 1, &Size);
    if (Err != FAT_OK) {
        return Err;
    }
    if (Size >= BufMax) {
        Size = BufMax - 1;
    }
    Buf[Size] = 0;
    *OutSize = Size;
    return FAT_OK;
}

int StoreReadAppDesktopMeta(const char *Id, STORE_APP_DESKTOP_META *Out) {
    char Path[128];
    char Buf[PKG_BUF_MAX];
    UINTN Size = 0;
    int Err;
    int HaveApps;
    PKG_PARSE_STATE State;

    if (!Id || !Id[0] || !Out) {
        return FAT_ERR_INVAL;
    }
    Out->DesktopYes = 0;
    Out->TaskbarYes = 0;
    Out->WindowYes = 0;
    Out->CategoryGame = 0;
    Out->Title[0] = 0;
    Out->IconRel[0] = 0;
    Out->FontRel[0] = 0;
    Out->Category[0] = 0;

    State.Out = Out;
    State.FillMissingOnly = 0;
    State.SeenDesktop = 0;
    State.SeenTaskbar = 0;
    State.SeenWindow = 0;
    State.SeenTitle = 0;
    State.SeenIcon = 0;
    State.SeenFont = 0;
    State.SeenCategory = 0;

    StoreAppBundleDir(Path, (int)sizeof(Path), Id);
    JoinPath(Path, (int)sizeof(Path), Path, "PKG.TXT");
    Err = ReadPkgFile(Path, Buf, sizeof(Buf), &Size);
    HaveApps = (Err == FAT_OK) ? 1 : 0;
    if (HaveApps) {
        ParsePackageBuffer(Buf, Size, &State);
    }

    /* 缺键用商店包补；Apps 已写明的 desktop=no 等不覆盖 */
    {
        char PkgDir[96];

        JoinPath(PkgDir, (int)sizeof(PkgDir), STORE_PACKAGES_DIR, Id);
        JoinPath(Path, (int)sizeof(Path), PkgDir, "PKG.TXT");
        Err = ReadPkgFile(Path, Buf, sizeof(Buf), &Size);
        if (Err == FAT_OK) {
            State.FillMissingOnly = HaveApps ? 1 : 0;
            ParsePackageBuffer(Buf, Size, &State);
        } else if (!HaveApps) {
            return Err;
        }
    }
    /*
     * 开窗 App 半截 PKG（只有 window=yes，无 desktop/taskbar）→ 菜单/桌面双丢。
     * Snake 有 category=game 走「游戏」不易察觉；Chat 只走「应用」+桌面会像没装。
     * 未写明的键才补；显式 desktop=no 不改。
     */
    if (Out->WindowYes && !Out->CategoryGame) {
        if (!State.SeenDesktop) {
            Out->DesktopYes = 1;
        }
        if (!State.SeenTaskbar) {
            Out->TaskbarYes = 1;
        }
    }
    return FAT_OK;
}
