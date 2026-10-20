/*
 * GuiPrivate.h — PR-R2：Gui 子模块共享状态与内部 API（非对外）
 */
#ifndef GUI_PRIVATE_H
#define GUI_PRIVATE_H

#include "Gui.h"
#include "BootTypes.h"

#define DRAG_MIN_STEP 3
#define DRAG_ROW_MAX  1920
/* 拖动脏区外扩：≥ ThemeWindowShadowSize（默认 6） */
#define DRAG_BORDER_PAD 8

#define MAX_WINS     GUI_MAX_WINS
#define TITLE_HEIGHT GUI_TITLE_HEIGHT
#define CLOSE_SIZE   24
#define CLOSE_MARGIN 6
/* 用户窗键队列深度；Poll 返回 300+HID（HID < 100，避开点击包 400+） */
#define GUI_USER_KEY_Q     8
#define GUI_USER_KEY_BASE  300
#define GUI_USER_KEY_HID_MAX 99
/*
 * 光标随分辨率缩放（基准 1080p：臂长 6、线宽 1px）。
 * 2160p ≈ 2×；gGuiWindowManager.Under 按上限预留（含黑描边外扩 1px）。
 */
#define CURSOR_HALF_BASE 6
#define CURSOR_REF_H     1080u
#define CURSOR_HALF_MAX  24
#define CURSOR_THICK_MAX 3
#define CURSOR_OUTLINE   1
/* +1：UI 缩放时 CursorBox 再外扩 1 逻辑像素 */
#define CURSOR_SCALE_PAD 3
/* resize 居中臂；指针头+尾最大约 4*HALF_MAX+描边 */
#define CURSOR_EXT_MAX   40
#define CURSOR_BOX       112

/* 光标外形：0=指针箭头；1..3=边/角 resize */
#define CURSOR_KIND_ARROW     0
#define CURSOR_KIND_RESIZE_SE 1
#define CURSOR_KIND_RESIZE_E  2
#define CURSOR_KIND_RESIZE_S  3

/* 改大小热区：右下角 / 右边 / 底边（不挪原点） */
#define RESIZE_EDGE_NONE 0
#define RESIZE_EDGE_SE   1
#define RESIZE_EDGE_E    2
#define RESIZE_EDGE_S    3

typedef struct {
    int      Active;
    int      FixedSize; /* 1：不可拖边改大小（写死客户区的用户窗） */
    GUI_WIN_KIND Kind;
    UINT32   X;
    UINT32   Y;
    UINT32   Width;
    UINT32   Height;
    UINT32   Background;
    const char *Title;
    UINT32   TermX;
    UINT32   TermY;
    int      TermSet;
    char     InputLine[GUI_INPUT_LINE_MAX];
    int      InputLen;
    int      WaitPrompt;
    int      PromptShown;
    char     TitleBuf[64];
    char     ClientText[1024]; /* 多行列表，如任务管理器 */
    int      ClosePending;
    volatile int Closing; /* CloseWindow 重入保护（exit 收窗 vs 点击关窗）；SMP 可见 */
} GUI_WINDOW;

/* 用户窗按钮、点击、键队列。与 Windows[] 同一个下标。拖动和终端行留在 GUI_WINDOW。 */
typedef struct {
    int      UserButtonUsed[4];
    char     UserButtonLabel[4][24];
    int      UserButtonClick;
    int      UserClientClick;
    UINT32   UserClickX;
    UINT32   UserClickY;
    UINT8    UserKeyQ[GUI_USER_KEY_Q];
    UINT8    UserKeyCount;
} GUI_USER_SLOT;

/* 窗口、光标、拖动、焦点。定义在 GuiWindowManager.c。 */
typedef struct {
    GUI_WINDOW Windows[MAX_WINS];
    GUI_USER_SLOT UserSlot[MAX_WINS];
    UINT32 ScreenWidth;
    UINT32 ScreenHeight;
    UINT32 CursorX;
    UINT32 CursorY;
    UINT8  CursorBtn;
    int    CursorKind;
    int    FocusWin;
    int    HoverWin;
    /* 指针和键盘的当前拥有者：拖动中是 DragWin，否则是 FocusWin。点击只记在这里。 */
    int    InputOwner;
    int    InputButton;
    int    InputClient;
    UINT32 InputClickX;
    UINT32 InputClickY;
    UINT32 SaveX;
    UINT32 SaveY;
    UINT32 SaveWidth;
    UINT32 SaveHeight;
    UINT32 Under[CURSOR_BOX * CURSOR_BOX];
    int    CursorVisible;
    int    DragWin;
    INT32  DragOffX;
    INT32  DragOffY;
    int    DragArmed;
    int    ResizeWin;
    int    ResizeArmed;
    int    ResizeEdge;
    UINT32 ResizeOrigW;
    UINT32 ResizeOrigH;
    INT32  ResizeAnchorX;
    INT32  ResizeAnchorY;
    int    ResizeBandOn;
    UINT32 ResizeBandW;
    UINT32 ResizeBandH;
    GUI_WINDOW WinSwap;
    int      DragHasBackup;
    int      WinBackupValid[MAX_WINS];
    UINT32   WinBackupW[MAX_WINS];
    UINT32   WinBackupH[MAX_WINS];
    UINT32   WinBackupPages[MAX_WINS];
    UINT32  *WinBackup[MAX_WINS];
    UINT32   DragRowBuf[DRAG_ROW_MAX];
    UINT32  *DragDirty;
    UINT32   DragDirtyPages;
    UINT32   DragDirtyCap;
    UINT32  *ScreenSnap;
    UINT32   ScreenSnapPages;
    int      ScreenSnapValid;
    UINT32  *UnderDrag;
    UINT32   UnderDragPages;
    int      UnderDragValid;
    UINT32   DragStartX;
    UINT32   DragStartY;
    UINT32   DragStartW;
    UINT32   DragStartH;
    int    GraphicsLockDepth;
    UINT64 GraphicsIrqFlags;
    volatile int ComposeBusy;
    int    DeferPresent;
    int    ShellEchoCoalesce;
    GUI_CONSOLE_OPS ConsoleOps;
    int    InputLocked;
    UINT8  MousePrevBtn;
} GUI_WINDOW_MANAGER;

extern GUI_WINDOW_MANAGER gGuiWindowManager;

static inline void GuiConsoleOpsFocusSave(void) {
    if (gGuiWindowManager.ConsoleOps.FocusSave) {
        gGuiWindowManager.ConsoleOps.FocusSave();
    }
}
static inline void GuiConsoleOpsFocusLoad(void) {
    if (gGuiWindowManager.ConsoleOps.FocusLoad) {
        gGuiWindowManager.ConsoleOps.FocusLoad();
    }
}
static inline void GuiConsoleOpsOnShellOpened(void) {
    if (gGuiWindowManager.ConsoleOps.OnShellOpened) {
        gGuiWindowManager.ConsoleOps.OnShellOpened();
    }
}
static inline void GuiConsoleOpsPaintShellWindow(int Idx) {
    if (gGuiWindowManager.ConsoleOps.PaintShellWindow) {
        gGuiWindowManager.ConsoleOps.PaintShellWindow(Idx);
    }
}

/* Compose / gfx */
void GraphicsIrqEnter(void);
void GraphicsIrqLeave(void);
void ComposeBegin(void);
void ComposeEnd(void);
int GuiInputOwner(void);
void ComposeBeginEraseCursor(void);
void GuiComposeDamage(void);
/* Close.c：USER 轻量关窗中；ThemeScene 见则 halt（gui-g3-close） */
int GuiCloseUserLightActive(void);
void GraphicsPresent(void);
UINT32 TitleBarColor(int Idx);
UINT32 TitleBarColorAtRow(int Idx, UINT32 Row, UINT32 TitleH);
UINT32 WindowBorderColor(int Idx);
int TopWindowAt(UINT32 X, UINT32 Y);
void GuiHoverUpdate(UINT32 X, UINT32 Y);
void CloseButtonRect(const GUI_WINDOW *W, UINT32 *Bx, UINT32 *By, UINT32 *Bw, UINT32 *Bh);
/* 标题文字在标题栏里垂直居中。 */
UINT32 TitleTextY(const GUI_WINDOW *W);
void DrawCloseButton(int Idx, const GUI_WINDOW *W);
int PixelOccludedByAbove(int Idx, UINT32 X, UINT32 Y);
void FillRectOccluded(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H, UINT32 Color);
void DrawHLineOccluded(int Idx, UINT32 X0, UINT32 X1, UINT32 Y, UINT32 Color);
void DrawVLineOccluded(int Idx, UINT32 X, UINT32 Y0, UINT32 Y1, UINT32 Color);
void DrawTitleStringOccluded(int Idx, const GUI_WINDOW *W);
void DrawWindowAtEx(int Idx, int Occlude);
void DrawWindowAt(int Idx);
void DrawWindowChromeAt(int Idx);
void DrawWindowShadowAt(int Idx);
/* 右/下 drop shadow：扩展 footprint（拖动清残影） */
void ExpandRectByWindowShadow(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
void RefreshOtherChrome(int SkipIdx);
void SyncWindowVisualsEx(int ClearDesktop);
void SyncWindowVisuals(void);
void FillDesktopRectClipped(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
int WindowOccludedByOther(int Idx);
int WindowOccludedByOtherOrShadow(int Idx);
int AllActiveWindowsHaveValidBackup(void);
UINT32 BackupPageCount(UINT32 Ww, UINT32 Wh);
int EnsureWindowBackupBuf(int Idx);
void PreallocWindowBackups(void);
void BackupWindowAtEx(int Idx, int ForceFull);
void BackupWindowAt(int Idx);
UINT32 AnalyticWindowPixel(int Idx, UINT32 Px, UINT32 Py);
void PaintWindowFromBackup(int Idx);
/* 只贴备份与矩形相交的部分；擦拖动脚印时用，避免整窗 WriteRect 盖住上层 */
void PaintWindowFromBackupClipped(int Idx, UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void ShiftWinBackupsUp(int From, int To);
/* PR-GUI-l3-fade：FadeIn=1 开窗，0 关窗；ThemeWindowFadeSteps()==0 则空操作 */
void GuiAnimateWindowFade(int Idx, int FadeIn);
int RectIntersects(UINT32 Ax, UINT32 Ay, UINT32 Aw, UINT32 Ah,
                   UINT32 Bx, UINT32 By, UINT32 Bw, UINT32 Bh);
void ClipRectToScreen(UINT32 *X, UINT32 *Y, UINT32 *W, UINT32 *H);
UINT32 SampleWindowBackupPixel(int Idx, UINT32 Px, UINT32 Py);
int WindowBackupCoversPixel(int Idx, UINT32 Px, UINT32 Py);
int PixelCoveredByHigherWindow(int Idx, UINT32 Px, UINT32 Py);

/* Cursor */
void CursorMetrics(int *Half, int *Thick);
void CursorBox(UINT32 Cx, UINT32 Cy, UINT32 *Sx, UINT32 *Sy, UINT32 *Sw, UINT32 *Sh);
void DrawCursorGlyph(UINT32 X, UINT32 Y);
void DrawCursorAxis(UINT32 X, UINT32 Y, int Axis);
void DrawCursorAt(UINT32 X, UINT32 Y);
void CursorRestore(void);
void CursorPaint(void);
void CursorMove(UINT32 X, UINT32 Y);
/* 指针边沿基准。和窗口状态放在一起。 */

/* Drag */
void ResetDragState(void);
int AnyWindowsOverlap(void);
UINT32 TopmostBelowDragPixel(UINT32 Px, UINT32 Py, int DragIdx);
int EnsureDragDirtyBuf(UINT32 Ww, UINT32 Hh);
int EnsureUnderDragBuf(UINT32 Ww, UINT32 Wh);
void CaptureDragRestoreData(int DragIdx);
void BeginDragBackups(int DragIdx);
void StartDragBackups(int DragIdx);
UINT32 CompositeDragPixel(UINT32 Px, UINT32 Py, int DragIdx,
                          UINT32 Nx, UINT32 Ny, UINT32 Nw, UINT32 Nh);
void CompositeDragDirtyRegion(int DragIdx, UINT32 OldX, UINT32 OldY,
                              UINT32 Ww, UINT32 Wh);
void RestoreWindowsInFootprint(UINT32 Fx, UINT32 Fy, UINT32 Fw, UINT32 Fh, int SkipIdx);
void ClearOldDragFootprint(UINT32 Ox, UINT32 Oy, UINT32 Ww, UINT32 Wh, int DragIdx);
void GuiClearIconDragFootprint(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
void PaintAllWindowsDraw(int DragIdx);
void RedrawDragFrame(int DragIdx, UINT32 OldX, UINT32 OldY);
/* CopyRect 平移 + 只擦露出条（减左右拖频闪） */
void RedrawDragFrameSlide(int DragIdx, UINT32 OldX, UINT32 OldY);
void GuiPollHoldDrain(UINT32 Sw, UINT32 Sh);
void GuiPollHoldApply(UINT32 *LastX, UINT32 *LastY, UINT8 *LastBtn,
                      INT8 *WheelSum, int *Any, int *NeedMove);
int GuiDragFrameTry(void);
void GuiDragFrameLeave(void);
void GuiDragFrameAccount(void);
void GuiDragFrameLog(void);
void ClampWindowPos(const GUI_WINDOW *W, INT32 *X, INT32 *Y);
void MoveWindowTo(int Idx, UINT32 NewX, UINT32 NewY);
void GuiDragUpdate(UINT32 X, UINT32 Y);
void GuiDragEnd(void);

/* PR-GUI-win-resize */
int PointInResizeCorner(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int GuiResizeEdgeAt(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int GuiResizeCursorKindAt(UINT32 X, UINT32 Y);
void GuiResizeBegin(int Idx, UINT32 X, UINT32 Y);
void GuiResizeUpdate(UINT32 X, UINT32 Y);
void GuiResizeEnd(void);
void EraseResizeBand(int Idx, UINT32 Bw, UINT32 Bh);
void DrawResizeBand(int Idx, UINT32 Bw, UINT32 Bh);
void ComputeResizeSize(int Idx, int Edge, UINT32 X, UINT32 Y,
                       UINT32 *OutW, UINT32 *OutH);
void RepaintAfterResize(int Idx);

/* Wm helpers */
void WinCopy(GUI_WINDOW *Dst, const GUI_WINDOW *Src);
int PointInClose(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
void CloseWindow(int Idx);
int PointInWindow(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int PointInTitle(const GUI_WINDOW *W, UINT32 X, UINT32 Y);
int PointInAnyActiveWindow(UINT32 X, UINT32 Y);
int PointOnAnyClose(UINT32 X, UINT32 Y);
void RaiseWindow(int Idx);
int AllocWindowSlot(void);
void PlaceNewWindow(int Idx, UINT32 *OutX, UINT32 *OutY, UINT32 *OutW, UINT32 *OutH);
/* 开窗辅助。定义在 GuiOpen.c；拆出应用开窗后同模块可见 */
void OpenChromeDefer(int Idx);
void OpenFadeIn(int Idx);
int FocusExistingKind(GUI_WIN_KIND Kind, void (*Repaint)(void), const char *LogTag);

/* User helpers */
void CopyTitleBuf(char *Dst, UINTN Cap, const char *Src);
void PaintUserClient(int Idx);
/* Raise 挪槽后用户态 wid 可能过期：解析到当前 USER 槽 */
int ResolveUserWindowIndex(int Wid);
int UserWindowIndexAfterRaise(int Wid);
void RepaintUserWindow(int Wid);
int UserButtonHit(int Idx, UINT32 X, UINT32 Y);
void GuiUserEnqueueKey(UINT8 HidKey);
int GuiUserDequeueKeyEvent(void);

/* PR-F-guiclick-1：GuiClick*.c */
int GuiClickTryContextMenu(UINT32 X, UINT32 Y);
int GuiClickTryDesktopBar(UINT32 X, UINT32 Y);
int GuiClickTryDesktopMiss(UINT32 X, UINT32 Y);
int GuiClickTryUserClient(UINT32 X, UINT32 Y);
int GuiClickTryRaiseWindow(UINT32 X, UINT32 Y);

#endif
