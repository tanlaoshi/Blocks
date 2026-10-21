/*
 * ListDetail.c — PaintList 右栏预览（PR-F-filesui-1）
 *
 * 从 List.c 抽出；与 Preview.c（内容装载）分工。
 */
#include "FilesUiPrivate.h"

void PaintListDrawPreview(UINT32 Cx, UINT32 Y, UINT32 H, UINT32 LineH, UINT32 Cw) {
    UINT32 ListW;
    UINT32 Px;
    UINT32 Py;
    UINT32 Pw;
    UINT32 InnerX;
    UINT32 InnerY;
    UINT32 InnerW;
    UINT32 InnerH;
    UINT32 CurY;
    UINTN ti;

    if (gPrevW == 0) {
        return;
    }

    ListW = Cw - gPrevW;
    Px = Cx + ListW;
    Py = Y + 8;
    Pw = gPrevW;

    gPrevX = Px;
    GraphicsFillRectangle(Px, Y, 2, H, ThemePanelSeparator());
    GraphicsFillRectangle(Px + 2, Y, Pw > 2 ? Pw - 2 : Pw, H, ThemePanelDetailBackground());
    DrawLine(Px + 10, Py, LocStr(MSG_FILES_PREVIEW), ThemeText());
    DrawLine(Px + 10, Py + LineH, gViewTitle[0] ? gViewTitle : LocStr(MSG_FILES_NONE),
             ThemeTextMuted());
    /* 同名卷：启动卷/启动盘 + DiskGUID（两行，避免撑破预览栏） */
    {
        UINT32 MetaY = Py + LineH * 2;
        UINT32 GuidMaxCols;
        UINT32 Adv;

        if (gVolIsBoot) {
            DrawLine(Px + 10, MetaY, LocStr(MSG_FILES_BOOT_VOL), ThemeTextAccent());
            MetaY += LineH;
        } else if (gVolOnBootDisk) {
            DrawLine(Px + 10, MetaY, LocStr(MSG_FILES_BOOT_DISK), ThemeTextAccent());
            MetaY += LineH;
        }
        if (gVolDiskGuid[0]) {
            char Line1[40];
            char Line2[40];
            int n = 0;
            const char *P = gVolDiskGuid;
            /* 启动卷/启动盘：disk+GUID 用强调色；旁路卷保持 muted */
            UINT32 GuidColor =
                (gVolIsBoot || gVolOnBootDisk) ? ThemeTextAccent() : ThemeTextMuted();

            Adv = FontAdvanceX() ? FontAdvanceX() : 8u;
            GuidMaxCols = (Pw > 24u) ? (Pw - 20u) / Adv : 16u;
            if (GuidMaxCols < 8u) {
                GuidMaxCols = 8u;
            }
            if (GuidMaxCols > sizeof(Line1) - 1) {
                GuidMaxCols = sizeof(Line1) - 1;
            }
            DrawLine(Px + 10, MetaY, "disk", GuidColor);
            MetaY += LineH;
            while (*P && n < (int)GuidMaxCols) {
                Line1[n++] = *P++;
            }
            Line1[n] = 0;
            DrawLine(Px + 10, MetaY, Line1, GuidColor);
            MetaY += LineH;
            if (*P) {
                n = 0;
                while (*P && n < (int)sizeof(Line2) - 1) {
                    Line2[n++] = *P++;
                }
                Line2[n] = 0;
                DrawLine(Px + 10, MetaY, Line2, GuidColor);
                MetaY += LineH;
            }
        }
        InnerY = MetaY + 8;
    }

    InnerX = Px + 8;
    InnerW = Pw > 16 ? Pw - 16 : Pw;
    InnerH = (Y + H > InnerY + 8) ? (Y + H - InnerY - 8) : 0;

    if (gPrevKind == PREV_EMPTY || gPrevKind == PREV_NONE) {
        if (InnerW > 8 && InnerH > LineH + 8) {
            GraphicsFillRectangle(InnerX, InnerY, InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                            ThemeDialogFace());
            GraphicsDrawRectangle(InnerX, InnerY,
                            InnerW, InnerH > LineH * 4 ? LineH * 4 : InnerH,
                            ThemePanelSeparator());
            DrawLine(InnerX + 10, InnerY + LineH,
                     LocStr(MSG_FILES_EMPTY), ThemeTextMuted());
        }
    } else if (gPrevKind == PREV_DIR) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_DIR), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_ENTER_OPEN), ThemeTextMuted());
    } else if (gPrevKind == PREV_ELF) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_ELF), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_ENTER_RUN), ThemeTextMuted());
    } else if (gPrevKind == PREV_ERR) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_CANT_READ), ThemeTextAccent());
    } else if (gPrevKind == PREV_BIN) {
        DrawLine(InnerX, InnerY, LocStr(MSG_FILES_BINARY), ThemeTextAccent());
        DrawLine(InnerX, InnerY + LineH, LocStr(MSG_FILES_HEX), ThemeTextMuted());
    } else if (gPrevKind == PREV_TEXT && InnerH > LineH) {
        char Row[72];
        int Col = 0;
        UINT32 MaxCols = (InnerW > 8) ? (InnerW - 4) / (FontAdvanceX() ? FontAdvanceX() : 8) : 20;

        if (MaxCols > sizeof(Row) - 1) {
            MaxCols = sizeof(Row) - 1;
        }
        if (MaxCols < 8) {
            MaxCols = 8;
        }
        CurY = InnerY;
        for (ti = 0; ti < gViewLen && CurY + LineH <= InnerY + InnerH; ti++) {
            char C = gView[ti];
            if (C == '\n' || Col >= (int)MaxCols) {
                Row[Col] = 0;
                DrawLine(InnerX, CurY, Row, ThemeText());
                CurY += LineH;
                Col = 0;
                if (C == '\n') {
                    continue;
                }
            }
            if (C == '\r') {
                continue;
            }
            if (C >= 32 && C < 127) {
                Row[Col++] = C;
            } else {
                Row[Col++] = '.';
            }
        }
        if (Col > 0 && CurY + LineH <= InnerY + InnerH) {
            Row[Col] = 0;
            DrawLine(InnerX, CurY, Row, ThemeText());
        }
    }
}
