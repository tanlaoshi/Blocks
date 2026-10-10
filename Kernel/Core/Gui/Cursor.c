/*
 * Cursor.c — 鼠标光标：前缓冲 save-under + 箭头字形
 *
 * 【初学者】
 * - 分层：Core/Gui；对标现网 GuiCursor（实现略薄；夹内不叠 Gui）
 * - 对外：GuiCursorHide / GuiCursorShow（Gui.h）；夹内 Cursor*（GuiPrivate.h）
 * - 只画 LFB 前缓冲，勿写背缓冲（否则 Present 后尖端「烤进」画面）
 */
#include "Gui.h"
#include "GuiPrivate.h"
#include "HalVideo.h"

#define CURSOR_COLOR   0x00FFFFFFu
#define CURSOR_OUTLINE 0x00000000u
#define CURSOR_WIDTH       11u
#define CURSOR_HEIGHT      16u
#define CURSOR_PAD         1
#define CURSOR_BOX_WIDTH   (CURSOR_WIDTH + 2u * (UINT32)CURSOR_PAD)
#define CURSOR_BOX_HEIGHT  (CURSOR_HEIGHT + 2u * (UINT32)CURSOR_PAD)

static int gCursorOn;
static int gCursorShown;
static INT32 gCurX;
static INT32 gCurY;
static UINT32 gFbW;
static UINT32 gFbH;
static UINT32 gUnder[CURSOR_BOX_HEIGHT][CURSOR_BOX_WIDTH];
static INT32 gSaveX;
static INT32 gSaveY;
static UINT32 gSaveW;
static UINT32 gSaveH;

/* MSB=左；尖在 (0,0)，与点击坐标一致 */
static const UINT8 gArrow[CURSOR_HEIGHT] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xF8, 0xDC, 0x8E, 0x07, 0x03, 0x01, 0x00, 0x00
};

/*
 * ArrowSolid — 箭头位图该像素是否实心
 *
 * 谁调用：仅 GuiCursorShow（描边与填色）。
 * 返回：1 实心；0 透明
 */
static int ArrowSolid(UINT32 Col, UINT32 Row) {
    if (Row >= CURSOR_HEIGHT || Col >= CURSOR_WIDTH) {
        return 0;
    }
    return (gArrow[Row] & (UINT8)(0x80u >> Col)) != 0;
}

/*
 * CursorSetFramebuffer — 记录 FB 尺寸供钳位与绝对坐标映射
 *
 * 谁调用：GuiInitialize。
 */
void CursorSetFramebuffer(UINT32 Width, UINT32 Height) {
    gFbW = Width;
    gFbH = Height;
}

/*
 * CursorEnableAt — 打开光标并放到 (X,Y)，立刻 Show
 *
 * 谁调用：GuiInitialize（HID/PS2 鼠就绪后）。
 * 前后文：前 — CursorSetFramebuffer；后 — GuiPoll 移动
 */
void CursorEnableAt(INT32 X, INT32 Y) {
    gCurX = X;
    gCurY = Y;
    gCursorOn = 1;
    gCursorShown = 0;
    CursorClamp();
    GuiCursorShow();
}

/*
 * CursorIsEnabled — 光标是否已启用
 *
 * 谁调用：GuiPoll 入口（未启用则不吃鼠包）。
 */
int CursorIsEnabled(void) {
    return gCursorOn;
}

/*
 * CursorGetPosition — 读出热点坐标
 *
 * 谁调用：GuiPoll（点击命中 / 拖窗）。
 */
void CursorGetPosition(INT32 *X, INT32 *Y) {
    if (X != 0) {
        *X = gCurX;
    }
    if (Y != 0) {
        *Y = gCurY;
    }
}

/*
 * CursorSetPosition — 绝对设置热点并钳位
 *
 * 谁调用：预留；当前主路径用 MoveBy / SetFromAbsolute。
 */
void CursorSetPosition(INT32 X, INT32 Y) {
    gCurX = X;
    gCurY = Y;
    CursorClamp();
}

/*
 * CursorMoveBy — 相对移动（PS/2 增量）
 *
 * 谁调用：GuiPoll 累加 Dx/Dy 后。
 */
void CursorMoveBy(INT32 DeltaX, INT32 DeltaY) {
    gCurX += DeltaX;
    gCurY += DeltaY;
    CursorClamp();
}

/*
 * CursorSetFromAbsolute — HID tablet 0..32767 → 像素
 *
 * 谁调用：GuiPoll（Pkt.Absolute）。
 */
void CursorSetFromAbsolute(INT32 PacketX, INT32 PacketY) {
    if (gFbW > 1u) {
        gCurX = (INT32)(((INT64)PacketX * (INT64)(gFbW - 1u)) / 32767);
    }
    if (gFbH > 1u) {
        gCurY = (INT32)(((INT64)PacketY * (INT64)(gFbH - 1u)) / 32767);
    }
    CursorClamp();
}

/*
 * CursorClamp — 热点限制在 FB 内
 *
 * 谁调用：本文件各移动/Enable 路径。
 */
void CursorClamp(void) {
    if (gCurX < 0) {
        gCurX = 0;
    }
    if (gCurY < 0) {
        gCurY = 0;
    }
    if (gFbW > 0 && (UINT32)gCurX >= gFbW) {
        gCurX = (INT32)(gFbW - 1u);
    }
    if (gFbH > 0 && (UINT32)gCurY >= gFbH) {
        gCurY = (INT32)(gFbH - 1u);
    }
}

/*
 * GuiCursorHide — 用 save-under 擦掉光标
 *
 * 做什么：把先前保存的像素写回前缓冲；不改热点。
 * 谁调用：GuiPoll 重画前；GuiRefreshLabels；Console 刷屏前后配对 Show。
 * 前后文：后 — 业务重绘；再 GuiCursorShow。
 */
void GuiCursorHide(void) {
    UINT32 Row;
    UINT32 Col;

    if (!gCursorShown) {
        return;
    }
    for (Row = 0; Row < gSaveH && Row < CURSOR_BOX_HEIGHT; Row++) {
        for (Col = 0; Col < gSaveW && Col < CURSOR_BOX_WIDTH; Col++) {
            INT32 Px = gSaveX + (INT32)Col;
            INT32 Py = gSaveY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, gUnder[Row][Col]);
            }
        }
    }
    gCursorShown = 0;
}

/*
 * GuiCursorShow — 保存 under 并画箭头（黑描边+白芯）
 *
 * 做什么：只写前缓冲；热点在箭头尖 (gCurX,gCurY)。
 * 谁调用：GuiPoll 末尾；GuiInitialize/RefreshLabels；Console 配对。
 * 返回：void（未 Enable 或无背缓冲则静默跳过）
 */
void GuiCursorShow(void) {
    UINT32 Row;
    UINT32 Col;
    INT32 Ox;
    INT32 Oy;

    if (!gCursorOn || gCursorShown || gFbW == 0) {
        return;
    }
    if (!HalVideoBackbufferEnabled()) {
        return;
    }
    Ox = gCurX - CURSOR_PAD;
    Oy = gCurY - CURSOR_PAD;
    gSaveX = Ox;
    gSaveY = Oy;
    gSaveW = CURSOR_BOX_WIDTH;
    gSaveH = CURSOR_BOX_HEIGHT;

    for (Row = 0; Row < CURSOR_BOX_HEIGHT; Row++) {
        for (Col = 0; Col < CURSOR_BOX_WIDTH; Col++) {
            INT32 Px = Ox + (INT32)Col;
            INT32 Py = Oy + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                gUnder[Row][Col] = HalVideoFrontReadPixel((UINT32)Px, (UINT32)Py);
            } else {
                gUnder[Row][Col] = 0;
            }
        }
    }

    /* 黑描边：实心邻域，尖在亮底也看得见 */
    for (Row = 0; Row < CURSOR_HEIGHT; Row++) {
        for (Col = 0; Col < CURSOR_WIDTH; Col++) {
            INT32 dRow;
            INT32 dCol;
            if (!ArrowSolid(Col, Row)) {
                continue;
            }
            for (dRow = -1; dRow <= 1; dRow++) {
                for (dCol = -1; dCol <= 1; dCol++) {
                    INT32 Ac = (INT32)Col + dCol;
                    INT32 Ar = (INT32)Row + dRow;
                    INT32 Px;
                    INT32 Py;
                    if (Ac >= 0 && Ar >= 0 && ArrowSolid((UINT32)Ac, (UINT32)Ar)) {
                        continue;
                    }
                    Px = gCurX + Ac;
                    Py = gCurY + Ar;
                    if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW &&
                        (UINT32)Py < gFbH) {
                        HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py,
                                              CURSOR_OUTLINE);
                    }
                }
            }
        }
    }
    for (Row = 0; Row < CURSOR_HEIGHT; Row++) {
        for (Col = 0; Col < CURSOR_WIDTH; Col++) {
            INT32 Px;
            INT32 Py;
            if (!ArrowSolid(Col, Row)) {
                continue;
            }
            Px = gCurX + (INT32)Col;
            Py = gCurY + (INT32)Row;
            if (Px >= 0 && Py >= 0 && (UINT32)Px < gFbW && (UINT32)Py < gFbH) {
                HalVideoFrontDrawPixel((UINT32)Px, (UINT32)Py, CURSOR_COLOR);
            }
        }
    }
    gCursorShown = 1;
}
