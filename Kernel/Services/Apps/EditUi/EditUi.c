/*
 * EditUi.c — 简易编辑器核心（缓冲、开关、保存）
 * 绘制：Paint.c；按键：Input.c。
 */
#include "EditUiPrivate.h"

char gPath[EDIT_PATH_MAX];
char gBuf[EDIT_BUF_MAX];
UINTN gEditLen;
UINTN gCursor;
int gScrollLine;
/* PR-18：用户用滚轮自己滚过之后，暂停「光标必须可见」的强制跟随 */
static int gScrollUser;
static UINTN gScrollLastCursor;
int gEditDirty;
char gEditStatus[EDIT_STATUS_MAX];

/* PR-GUI-migrate-edit：Save 钮（SYNC → EditUiSave） */
UI_BUTTON_ACTION gEditSave;

static void EditSaveAction(void *Ctx) {
    (void)Ctx;
    EditUiSave();
}

static void EditSaveInit(void) {
    gEditSave.Button.X = 0;
    gEditSave.Button.Y = 0;
    gEditSave.Button.W = 72;
    gEditSave.Button.H = 24;
    gEditSave.Button.Text = LocStr(MSG_EDIT_SAVE);
    gEditSave.Button.Enabled = 0;
    gEditSave.Button.Visible = 0;
    gEditSave.Button.m_State = UI_BUTTON_STATE_NORMAL;
    gEditSave.Kind = UI_ACTION_SYNC;
    gEditSave.Fn = EditSaveAction;
    gEditSave.Ctx = 0;
}

void EditUiOnWheel(INT8 Wheel) {
    if (Wheel == 0 || !EditUiIsFocused()) {
        return;
    }
    /* 正滚轮 = 看列表上方；不改变光标，只改视口 */
    gScrollUser = 1;
    gScrollLine -= (int)Wheel;
    EditUiRepaint();
}

void EditSetStatus(const char *S) {
    StringCopy(gEditStatus, sizeof(gEditStatus), S ? S : "");
}

UINTN LineStartOf(UINTN Pos) {
    UINTN i = Pos;

    while (i > 0 && gBuf[i - 1] != '\n') {
        i--;
    }
    return i;
}

UINTN LineIndexOf(UINTN Pos) {
    UINTN i;
    UINTN Line = 0;

    for (i = 0; i < Pos && i < gEditLen; i++) {
        if (gBuf[i] == '\n') {
            Line++;
        }
    }
    return Line;
}

void EditClampScroll(UINT32 VisLines) {
    UINTN CurLine = LineIndexOf(gCursor);

    if (VisLines < 1) {
        VisLines = 1;
    }
    /* 光标自己动了（方向键/输入）→ 恢复跟随光标 */
    if (gCursor != gScrollLastCursor) {
        gScrollLastCursor = gCursor;
        gScrollUser = 0;
    }
    if (gScrollUser) {
        /* 用户滚过：只把滚动位置夹在有效行范围内，别把光标硬拉回视野 */
        UINTN LastLine = LineIndexOf(gEditLen);
        int MaxTop;

        MaxTop = (LastLine + 1u >= VisLines) ? (int)(LastLine + 1u - VisLines) : 0;
        if (gScrollLine > MaxTop) {
            gScrollLine = MaxTop;
        }
        if (gScrollLine < 0) {
            gScrollLine = 0;
        }
        return;
    }
    if ((UINTN)gScrollLine > CurLine) {
        gScrollLine = (int)CurLine;
    }
    if (CurLine >= (UINTN)gScrollLine + VisLines) {
        gScrollLine = (int)(CurLine - VisLines + 1);
    }
    if (gScrollLine < 0) {
        gScrollLine = 0;
    }
}

int InsertChar(char C) {
    UINTN i;

    if (gEditLen + 1 >= EDIT_BUF_MAX) {
        EditSetStatus(LocStr(MSG_EDIT_BUF_FULL));
        return 0;
    }
    for (i = gEditLen; i > gCursor; i--) {
        gBuf[i] = gBuf[i - 1];
    }
    gBuf[gCursor] = C;
    gEditLen++;
    gCursor++;
    gBuf[gEditLen] = 0;
    gEditDirty = 1;
    EditSetStatus(LocStr(MSG_EDIT_MODIFIED));
    return 1;
}

void DeleteAt(UINTN Pos) {
    UINTN i;

    if (Pos >= gEditLen) {
        return;
    }
    for (i = Pos; i + 1 < gEditLen; i++) {
        gBuf[i] = gBuf[i + 1];
    }
    gEditLen--;
    gBuf[gEditLen] = 0;
    gEditDirty = 1;
    EditSetStatus(LocStr(MSG_EDIT_MODIFIED));
}

void EditUiSave(void) {
    int Err;

    if (gPath[0] == 0) {
        EditSetStatus(LocStr(MSG_EDIT_NO_PATH));
        EditUiRepaint();
        return;
    }
    Err = FileSystemWriteFile(gPath, gBuf, gEditLen);
    if (Err != FAT_OK) {
        EditSetStatus(FatStrError(Err));
    } else {
        gEditDirty = 0;
        EditSetStatus(LocStr(MSG_EDIT_SAVED));
    }
    EditUiRepaint();
}

void EditUiOpen(const char *Path) {
    UINTN N = 0;
    int Err;

    EditSaveInit();
    gEditLen = 0;
    gCursor = 0;
    gScrollLine = 0;
    gEditDirty = 0;
    gBuf[0] = 0;
    gPath[0] = 0;
    EditSetStatus("");

    if (!Path || !Path[0]) {
        EditSetStatus(LocStr(MSG_EDIT_BAD_PATH));
        return;
    }
    StringCopy(gPath, sizeof(gPath), Path);
    Err = FileSystemReadFile(gPath, gBuf, sizeof(gBuf) - 1, &N);
    if (Err == FAT_OK) {
        gEditLen = N;
        gBuf[gEditLen] = 0;
        EditSetStatus(LocStr(MSG_EDIT_READY));
    } else if (Err == FAT_ERR_NOENT) {
        gEditLen = 0;
        gBuf[0] = 0;
        gEditDirty = 1;
        EditSetStatus(LocStr(MSG_EDIT_NEW));
    } else {
        EditSetStatus(FatStrError(Err));
    }
    gCursor = gEditLen;
}

int EditUiIsFocused(void) {
    return GuiFocusKind() == GUI_WIN_EDIT;
}
