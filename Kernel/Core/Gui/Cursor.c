/*
 * Cursor.c — 鼠标光标：前缓冲 save-under + 箭头字形
 *
 * 对标现网 CodeD-Services/GuiCursor/（实现略薄；目录即命名空间不叠 Gui）。
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

static int ArrowSolid(UINT32 Col, UINT32 Row) {
    if (Row >= CURSOR_HEIGHT || Col >= CURSOR_WIDTH) {
        return 0;
    }
    return (gArrow[Row] & (UINT8)(0x80u >> Col)) != 0;
}

void CursorSetFramebuffer(UINT32 Width, UINT32 Height) {
    gFbW = Width;
    gFbH = Height;
}

void CursorEnableAt(INT32 X, INT32 Y) {
    gCurX = X;
    gCurY = Y;
    gCursorOn = 1;
    gCursorShown = 0;
    CursorClamp();
    GuiCursorShow();
}

int CursorIsEnabled(void) {
    return gCursorOn;
}

void CursorGetPosition(INT32 *X, INT32 *Y) {
    if (X != 0) {
        *X = gCurX;
    }
    if (Y != 0) {
        *Y = gCurY;
    }
}

void CursorSetPosition(INT32 X, INT32 Y) {
    gCurX = X;
    gCurY = Y;
    CursorClamp();
}

void CursorMoveBy(INT32 DeltaX, INT32 DeltaY) {
    gCurX += DeltaX;
    gCurY += DeltaY;
    CursorClamp();
}

void CursorSetFromAbsolute(INT32 PacketX, INT32 PacketY) {
    if (gFbW > 1u) {
        gCurX = (INT32)(((INT64)PacketX * (INT64)(gFbW - 1u)) / 32767);
    }
    if (gFbH > 1u) {
        gCurY = (INT32)(((INT64)PacketY * (INT64)(gFbH - 1u)) / 32767);
    }
    CursorClamp();
}

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
 * 光标只叠前缓冲（LFB）。写背缓冲会在 Present 后把尖端「烤进」画面，
 * Hide 再拿旧 under 盖回去 → 看起来尖偏了、点不中。
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
