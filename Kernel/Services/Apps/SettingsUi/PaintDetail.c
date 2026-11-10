/*
 * PaintDetail.c — Settings 详情栏（自 Paint.c 纯搬 · PR-NORM-16）
 */
#include "SettingsUiPrivate.h"

/* 详情栏软换行；返回 1=画了字 */
static int DrawDetailText(UINT32 X, UINT32 *Ty, UINT32 MaxY, UINT32 MaxW,
                          UINT32 LineStep, const char *S, UINT32 Color) {
    UINT32 Ny;

    if (!S || !Ty || *Ty >= MaxY) {
        return 0;
    }
    Ny = UiDrawTextWrap(X, *Ty, MaxW, MaxY, LineStep, S, Color);
    if (Ny <= *Ty) {
        return 0;
    }
    *Ty = Ny;
    return 1;
}

void DrawDetail(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    UINT32 LineH;
    UINT32 Pad;
    UINT32 Gap;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 TextW;
    UINT32 MaxTextW;
    UINT32 SwW;
    UINT32 SwH;
    UINT32 Step;
    char Line[64];
    char Item[40];
    UINT32 Swatch;
    UINT32 PrefW;
    UINT32 PrefH;
    UINT32 NowW;
    UINT32 NowH;
    int HasBody;

    LineH = UiLayoutRowH();
    Step = FontAdvanceY() + 2u;
    if (Step < 14u) {
        Step = 14u;
    }
    Pad = UI_LAYOUT_PAD;
    Gap = UI_LAYOUT_GAP;
    MaxTextW = W > Pad * 2u ? W - Pad * 2u : W;
    GraphicsFillRectangle(X, Y, W, H, ThemePanelDetailBackground());
    if (W > UI_LAYOUT_SEP_W) {
        GraphicsFillRectangle(X, Y, UI_LAYOUT_SEP_W, H, ThemePanelSeparator());
    }

    ItemLabel(gItemSel, Item, (int)sizeof(Item));
    HasBody = (Item[0] != 0) || gDisplayHint != 0;

    if (!HasBody) {
        /* 空态：垂直居中一行标题 + 一行弱提示 */
        Ty = Y + (H > LineH * 2u + Gap ? (H - LineH * 2u - Gap) / 2u : Pad);
        MaxY = Y + H - Pad;
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_DEV_DETAIL),
                             ThemeText());
        Ty += Gap;
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_CLICK_APPLY),
                             ThemeTextMuted());
        return;
    }

    Ty = Y + Pad;
    MaxY = Y + H - Pad;
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_DEV_DETAIL),
                         ThemeText());
    Ty += Gap;
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, CatLabel(gCat),
                         ThemeTextMuted());
    Ty += Gap;

    if (Item[0] && Ty + Step <= MaxY) {
        if ((gCat == SETTINGS_CAT_DESKTOP || gCat == SETTINGS_CAT_SHELL) &&
            Ty + Step <= MaxY && W > Pad * 2u + 48u) {
            Swatch = (gCat == SETTINGS_CAT_DESKTOP)
                         ? ((gItemSel >= 0 && gItemSel < DESKTOP_COLOR_COUNT &&
                             gDesktopColors[gItemSel].Color != DESKTOP_COLOR_WALLPAPER)
                                ? gDesktopColors[gItemSel].Color
                                : ThemeDesktopBackground())
                         : ((gItemSel >= 0 && gItemSel < SHELL_COLOR_COUNT)
                                ? gShellColors[gItemSel].Color
                                : ThemeShellClientBackground());
            TextW = FontStringWidth(Item);
            SwH = LineH > 8u ? LineH - 4u : LineH;
            SwW = 48u;
            {
                UINT32 ItemY = Ty + (LineH > FontCellH() ? (LineH - FontCellH()) / 2u : 0);
                (void)DrawDetailText(X + Pad, &ItemY, MaxY, MaxTextW, Step, Item, ThemeText());
            }
            if (Pad + TextW + Gap + SwW + Pad <= W) {
                GraphicsFillRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH, Swatch);
                GraphicsDrawRectangle(X + Pad + TextW + Gap, Ty + 2, SwW, SwH,
                                ThemePanelSeparator());
            }
            Ty += LineH + Gap;
        } else {
            (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Item, ThemeText());
            Ty += Gap;
        }
    }

    if (gCat == SETTINGS_CAT_FONT && Ty + Step <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_FONT_SAMPLE),
                             ThemeText());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_DISPLAY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step,
                             LocaleString(HalCpuIsHypervisor() ? MESSAGE_SET_DISP_QEMU
                                                         : MESSAGE_SET_DISP_PC),
                             ThemeTextMuted());
        FormatNowDisplay(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        FormatNowScale(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        if (ThemeHasDisplayPref()) {
            PrefW = ThemeDisplayWidth();
            PrefH = ThemeDisplayHeight();
            HalVideoGetPhysicalSize(&NowW, &NowH);
            if (NowW == 0 || NowH == 0) {
                HalVideoGetSize(&NowW, &NowH);
            }
            if (PrefW != NowW || PrefH != NowH) {
                (void)DrawDetailText(
                    X + Pad, &Ty, MaxY, MaxTextW, Step,
                    LocaleString(HalCpuIsHypervisor() ? MESSAGE_SET_PREF_DIFF : MESSAGE_SET_PREF_DIFF_PC),
                    ThemeTextAccent());
            }
        }
    } else if (gCat == SETTINGS_CAT_SCALE && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_SCALE_HINT1),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_SCALE_HINT2),
                             ThemeTextMuted());
        Ty += Gap;
        FormatNowDisplay(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        FormatNowScale(Line, sizeof(Line));
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, Line, ThemeTextMuted());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_THEME && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_THEME_HINT),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_LIVE_DB),
                             ThemeTextMuted());
        Ty += Gap;
    } else if (gCat == SETTINGS_CAT_EFFECTS && Ty + Step * 2u <= MaxY) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_EFFECTS_HINT),
                             ThemeTextMuted());
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_LIVE_DB),
                             ThemeTextMuted());
        Ty += Gap;
    }

    if (gDisplayHint == 2) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_APPLIED_LIVE),
                             ThemeTextAccent());
    } else if (gDisplayHint == 1) {
        (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step,
                             LocaleString(HalCpuIsHypervisor() ? MESSAGE_SET_SAVED : MESSAGE_SET_SAVED_PC),
                             ThemeTextAccent());
    }
    (void)DrawDetailText(X + Pad, &Ty, MaxY, MaxTextW, Step, LocaleString(MESSAGE_SET_CLICK_APPLY),
                         ThemeTextMuted());
}

