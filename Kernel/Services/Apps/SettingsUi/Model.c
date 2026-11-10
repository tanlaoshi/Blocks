/*
 * SettingsUiModel.c — 分类/条目文案与显示模式表
 * 核心：SettingsUi.c
 */
#include "SettingsUiPrivate.h"
#include "Hal/Ksend.h"

const char *CatLabel(SETTINGS_CAT C) {
    /* 三分栏左侧短名（locale 已去掉「N.」与括号说明） */
    switch (C) {
    case SETTINGS_CAT_DESKTOP:  return LocaleString(MESSAGE_SET_DESKTOP_BG);
    case SETTINGS_CAT_SHELL:    return LocaleString(MESSAGE_SET_SHELL_BG);
    case SETTINGS_CAT_FONT:     return LocaleString(MESSAGE_SET_FONT);
    case SETTINGS_CAT_DISPLAY:  return LocaleString(MESSAGE_SET_DISPLAY);
    case SETTINGS_CAT_LANGUAGE: return LocaleString(MESSAGE_SET_LANGUAGE);
    case SETTINGS_CAT_SCALE:    return LocaleString(MESSAGE_SET_SCALE);
    case SETTINGS_CAT_THEME:    return LocaleString(MESSAGE_SET_THEME);
    case SETTINGS_CAT_EFFECTS:  return LocaleString(MESSAGE_SET_EFFECTS);
    case SETTINGS_CAT_HOTSEND:  return LocaleString(MESSAGE_SET_HOTSEND);
    default:                    return "?";
    }
}

void EnsureDisplayModes(void) {
    UINT32 N;
    UINT32 i;
    UINT32 W;
    UINT32 H;
    UINTN L;

    if (gModesReady) {
        return;
    }
    gModesReady = 1;
    gModeCount = 0;
    N = HalVideoModeCount();
    if (N > BOOT_VIDEO_MODE_MAX) {
        N = BOOT_VIDEO_MODE_MAX;
    }
    for (i = 0; i < N; i++) {
        if (HalVideoModeGet(i, &W, &H) != 0 || W < 640 || H < 480) {
            continue;
        }
        FormatUxU(gModeLabels[gModeCount], sizeof(gModeLabels[0]), W, H);
        L = 0;
        while (gModeLabels[gModeCount][L]) {
            L++;
        }
        gModes[gModeCount].Label = gModeLabels[gModeCount];
        gModes[gModeCount].W = W;
        gModes[gModeCount].H = H;
        gModeCount++;
    }
    if (gModeCount == 0) {
        N = (UINT32)(sizeof(gModesFallback) / sizeof(gModesFallback[0]));
        for (i = 0; i < N && gModeCount < BOOT_VIDEO_MODE_MAX; i++) {
            gModes[gModeCount] = gModesFallback[i];
            gModeCount++;
        }
    }
}

int ModeCount(void) {
    EnsureDisplayModes();
    return gModeCount;
}

int ItemCount(void) {
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:  return DESKTOP_COLOR_COUNT;
    case SETTINGS_CAT_SHELL:    return SHELL_COLOR_COUNT;
    case SETTINGS_CAT_FONT:     return (int)FontCount();
    case SETTINGS_CAT_DISPLAY:  return 1 + ModeCount();
    case SETTINGS_CAT_LANGUAGE: return LocaleCount();
    case SETTINGS_CAT_SCALE:    return SCALE_COUNT;
    case SETTINGS_CAT_THEME:    return THEME_CHOICE_COUNT;
    case SETTINGS_CAT_EFFECTS:  return EFFECTS_CHOICE_COUNT;
    case SETTINGS_CAT_HOTSEND:  return HOTSEND_ITEM_COUNT;
    default:                    return 0;
    }
}


void ItemLabel(int Idx, char *Out, int OutMax) {
    UINT32 Sc;
    int P;

    if (OutMax <= 0) {
        return;
    }
    Out[0] = 0;
    if (Idx < 0 || Idx >= ItemCount()) {
        return;
    }
    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        StringCopy(Out, OutMax, LocaleString(gDesktopColors[Idx].Msg));
        break;
    case SETTINGS_CAT_SHELL:
        StringCopy(Out, OutMax, LocaleString(gShellColors[Idx].Msg));
        break;
    case SETTINGS_CAT_FONT:
        /* cjk-4：UMing 标（中英）；Sun/Terminus 标（仅英文） */
        FontFaceLabel((UINT32)Idx, Out, OutMax);
        break;
    case SETTINGS_CAT_DISPLAY:
        if (Idx == 0) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_DISP_AUTO));
        } else {
            StringCopy(Out, OutMax, gModes[Idx - 1].Label);
        }
        break;
    case SETTINGS_CAT_LANGUAGE: {
        const char *Code = LocaleCodeAt(Idx);
        int p = 0;
        int c = 0;
        int n = Idx + 1;

        if (OutMax < 2) {
            break;
        }
        if (n >= 10 && p < OutMax - 1) {
            Out[p++] = (char)('0' + (n / 10) % 10);
        }
        if (p < OutMax - 1) {
            Out[p++] = (char)('0' + (n % 10));
        }
        if (p < OutMax - 1) {
            Out[p++] = '.';
        }
        if (p < OutMax - 1) {
            Out[p++] = ' ';
        }
        while (Code[c] != 0 && p < OutMax - 1) {
            Out[p++] = Code[c++];
        }
        Out[p] = 0;
        break;
    }
    case SETTINGS_CAT_SCALE:
        Sc = gScales[Idx];
        P = 0;
        if (Sc >= 100) {
            Out[P++] = (char)('0' + (Sc / 100) % 10);
        }
        Out[P++] = (char)('0' + (Sc / 10) % 10);
        Out[P++] = (char)('0' + (Sc % 10));
        Out[P++] = '%';
        Out[P] = 0;
        break;
    case SETTINGS_CAT_THEME:
        if (Idx == 0) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_THEME_DEFAULT));
        } else if (Idx == 1) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_THEME_MODERN));
        } else {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_THEME_TECH));
        }
        break;
    case SETTINGS_CAT_EFFECTS:
        if (Idx == 0) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_EFFECTS_MINIMAL));
        } else if (Idx == 1) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_EFFECTS_LOW));
        } else if (Idx == 2) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_EFFECTS_MEDIUM));
        } else {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_EFFECTS_HIGH));
        }
        break;
    case SETTINGS_CAT_HOTSEND: {
        char PortLabel[16];
        UINT16 Port;
        int Length;

        if (Idx == 0) {
            if (KsendServeState() == KSEND_SERVE_LISTENING ||
                KsendServeState() == KSEND_SERVE_TRANSFERRING) {
                StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_STOP));
            } else {
                StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_START));
            }
            break;
        }
        if (Idx >= 1 && Idx <= HOTSEND_PORT_COUNT) {
            Port = HotSendPortAt(Idx - 1);
            Length = 0;
            PortLabel[Length++] = 'P';
            PortLabel[Length++] = 'o';
            PortLabel[Length++] = 'r';
            PortLabel[Length++] = 't';
            PortLabel[Length++] = ' ';
            if (Port >= 10000u) {
                PortLabel[Length++] = (char)('0' + (Port / 10000u) % 10u);
            }
            if (Port >= 1000u) {
                PortLabel[Length++] = (char)('0' + (Port / 1000u) % 10u);
            }
            PortLabel[Length++] = (char)('0' + (Port / 100u) % 10u);
            PortLabel[Length++] = (char)('0' + (Port / 10u) % 10u);
            PortLabel[Length++] = (char)('0' + (Port % 10u));
            PortLabel[Length] = 0;
            StringCopy(Out, OutMax, PortLabel);
            break;
        }
        if (Idx == 4) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_REBOOT_ON));
        } else if (Idx == 5) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_REBOOT_OFF));
        } else if (Idx == 6) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_BACKUP_ON));
        } else if (Idx == 7) {
            StringCopy(Out, OutMax, LocaleString(MESSAGE_SET_HOTSEND_BACKUP_OFF));
        }
        break;
    }
    default:
        break;
    }
}

/* 当前已生效值在列表中的下标 */
int CurrentItemIndex(void) {
    int i;
    UINT32 Cur;
    UINT32 PrefW;
    UINT32 PrefH;
    int HasPref;

    switch (gCat) {
    case SETTINGS_CAT_DESKTOP:
        if (ThemeWallpaperEnabled()) {
            return 0;
        }
        Cur = ThemeDesktopBackground();
        for (i = 1; i < DESKTOP_COLOR_COUNT; i++) {
            if (gDesktopColors[i].Color == Cur) {
                return i;
            }
        }
        return 1;
    case SETTINGS_CAT_SHELL:
        Cur = ThemeShellClientBackground();
        for (i = 0; i < SHELL_COLOR_COUNT; i++) {
            if (gShellColors[i].Color == Cur) {
                return i;
            }
        }
        return 0;
    case SETTINGS_CAT_FONT:
        return (int)ThemeFontId();
    case SETTINGS_CAT_DISPLAY:
        HasPref = ThemeHasDisplayPref();
        if (!HasPref) {
            return 0;
        }
        PrefW = ThemeDisplayWidth();
        PrefH = ThemeDisplayHeight();
        for (i = 0; i < ModeCount(); i++) {
            if (gModes[i].W == PrefW && gModes[i].H == PrefH) {
                return i + 1;
            }
        }
        return 0;
    case SETTINGS_CAT_LANGUAGE:
        return LocaleIndex();
    case SETTINGS_CAT_SCALE:
        Cur = ThemeUiScale();
        for (i = 0; i < SCALE_COUNT; i++) {
            if (gScales[i] == Cur) {
                return i;
            }
        }
        return 1;
    case SETTINGS_CAT_THEME:
        if (ThemeThemeId() == THEME_PALETTE_DEFAULT) {
            return 0;
        }
        if (ThemeThemeId() == THEME_PALETTE_MODERN) {
            return 1;
        }
        return 2;
    case SETTINGS_CAT_EFFECTS:
        return (int)ThemeGetEffectLevel();
    case SETTINGS_CAT_HOTSEND: {
        UINT16 Port = KsendServePreferencePort();
        int PortIndex;

        for (PortIndex = 0; PortIndex < HOTSEND_PORT_COUNT; PortIndex++) {
            if (HotSendPortAt(PortIndex) == Port) {
                return PortIndex + 1;
            }
        }
        return 1;
    }
    default:
        return 0;
    }
}
