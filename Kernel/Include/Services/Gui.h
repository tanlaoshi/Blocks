/*
 * Gui.h — 简易窗口管理器与鼠标光标
 *
 * 稳态开窗故事（story-s1）：PollMouse → GuiHandleClick → Desktop Action → GuiOpen*。
 * 合成/脏/拖见 Compose·Damage·Drag 头（gui-g0…g8）；本头只挂对外 API。
 */
#ifndef GUI_H
#define GUI_H

#include "BootTypes.h"

typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons; /* bit0=左 bit1=右 bit2=中（PR-I1） */
    INT8   Wheel;   /* 有符号滚轮；Gui 消费见 I2 */
} GUI_MOUSE_STATE;

/* PR-D3：窗口种类（桌面图标 / Settings 依赖） */
typedef enum {
    GUI_WIN_NONE = 0,
    GUI_WIN_SHELL,
    GUI_WIN_SETTINGS,
    GUI_WIN_FILES,
    GUI_WIN_STORE, /* PR-G-store-ui */
    GUI_WIN_DEVICES, /* PR-DEV-6 */
    GUI_WIN_EDIT,  /* PR-V2：简易文本编辑器 */
    GUI_WIN_TTY,   /* PR-GUI-tty-win：串口会话窗 */
    GUI_WIN_USER   /* PR-G14：用户态窗口协议 */
} GUI_WIN_KIND;

#define GUI_MAX_WINS        6
#define GUI_TITLE_HEIGHT    40
#define GUI_CLIENT_PAD 8 /* 与 ThemeClientPadding() 对齐（PR-GUI-l1） */
#define GUI_INPUT_LINE_MAX  128

/* PR-R2：Console→Gui 单向；Gui 经回调调 Shell 绘制/焦点，避免互 #include */
typedef struct {
    void (*FocusSave)(void);
    void (*FocusLoad)(void);
    void (*OnShellOpened)(void);
    void (*PaintShellWindow)(int Idx);
} GUI_CONSOLE_OPS;

void GuiRegisterConsoleOps(const GUI_CONSOLE_OPS *Ops);

void GuiDesktopInitialize(void);
/* PR-G-hotres：HalVideoSetMode 成功后钳窗/光标并整桌重绘 */
void GuiOnDisplayResize(void);
void GuiPointerMove(UINT32 X, UINT32 Y);
/* 光标契约（Erase/Busy Flush/分脏）见 Cursor.c / Compose.c 头 gui-g7-cursor */
void GuiCursorPaint(void);
void GuiFrameBufferBegin(void);
void GuiFrameBufferEnd(void);
/* Defer 计数：GraphicsPresent 吞帧；对照见 Compose.c 头 gui-g1-defer */
void GuiPresentDeferPush(void);
void GuiPresentDeferPop(void); /* Defer==0 时 Present */
/* 最外层暂清 Defer，放行光标（等 Job / 拖）；可嵌套 */
void GuiPresentDeferPause(void);
void GuiPresentDeferResume(void);
/* 打字回显：跳过本帧 GraphicsPresent；Shell 环末合并 */
void GuiPresentShellEchoMark(void);
/* gDragFrame|ComposeBusy：他核/Shell 勿 Present；Blocked→HoldDrain 见 gui-g8-drag */
int GuiPresentBlocked(void);
/* DragWin|ResizeWin：ComposeDamage 早退（≠ gDragFrame） */
int GuiDragActive(void);
void GuiCursorHide(void);
void GuiCursorShow(void);
/*
 * 开机慢活后再露光标：HID 可早绑；未就绪不 Present / 不跟坐标 / 不点按。
 * Worker 在 AnnounceBootReady 前 Reveal（Secondary+图标+网/iwl 静默后）：
 * 居中 + Handoff + 首次画出。一露即须流畅。
 */
int GuiCursorUsbReady(void);
void GuiCursorRevealAfterUsb(void);
/* 空拖保鼠：有位移时记时；Idle 后才允许钟栏 Present / Worker HotPoll+RTC */
void GuiPointerNoteActivity(void);
int GuiPointerIdleForMs(UINT32 Ms);
void GuiOnMouse(const GUI_MOUSE_STATE *Mouse);
/* 右键：桌面弹出菜单；点在窗口上则收起 */
void GuiRightClickPlaceholder(UINT32 X, UINT32 Y);
void GuiOnArrowKey(UINT8 Key);
void GuiRedraw(void);
/* 桌面刷新入口（菜单快路径 / Damage）；管线见 Compose 下 .c 头 gui-g0-map */
void GuiRefreshDesktop(void);
/* PR-D2：按 Theme 刷新已开窗客户区底色（不立即重绘） */
void GuiApplyThemeColors(void);
/* 整屏画家算法回退（无 Gui Mark 时）；见 Compose.c */
void GuiComposeThemeScene(void);
/* 并 Gui 合成脏 AABB；下次 Refresh/Damage 只画这块（≠ HAL 脏） */
void GuiMarkDamage(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* 按当前几何重画整窗（标题栏+客户区底）；不画 Shell 文字 / Settings 菜单 */
void GuiPaintWindow(int Idx);
void GuiBackupAllWindows(void);
/* 点是否落在任一应用窗内（桌面图标绘制避让） */
int GuiPointInAnyWindow(UINT32 X, UINT32 Y);
int GuiHandleClick(UINT32 X, UINT32 Y);
int GuiFocusClient(UINT32 *X, UINT32 *Y, UINT32 *Width, UINT32 *Height, UINT32 *Background);
void GuiFocusSave(void);
void GuiFocusApply(void);
void GuiFocusApplyClip(void);
void GuiFocusSyncCursor(void);
void GuiBackupSyncRect(UINT32 X, UINT32 Y, UINT32 W, UINT32 H);
/* 把焦点窗当前帧缓冲整窗抓进备份（Shell/Settings 画完内容后调用） */
void GuiBackupFocusWindow(void);
void GuiFocusClearClient(void);
int GuiShellAcceptsInput(void);
void GuiFocusHome(void);
void GuiPollMouse(void);
/* 长 IO（Store 拷贝）期间：只挪光标、吞按键边沿，避免点按卡死到操作结束 */
void GuiPollMouseMotion(void);
/* ThemeSave / 热切：InputLocked 挡 GuiTask 的 GuiPollMouse（防 vvfat 重入写） */
void GuiInputLock(int Locked);
int GuiInputLocked(void);
int GuiShellWindowActive(int Idx);
void GuiSetFocusWindow(int Idx);
/* 置顶 + 从备份重合成；内容绘制前应调用，避免写穿上层窗 */
void GuiRaiseToFront(int Idx);
/* 当前焦点窗下标；无焦点返回 -1 */
int GuiFocusIndex(void);

/* PR-D3：打开应用窗；成功返回槽位，失败 -1。
 * Settings/Store/Edit/Files/Devices 单例（已有则前置）；Shell 可多开。 */
int GuiOpenShell(void);
int GuiOpenSettings(void);
int GuiOpenFiles(void);
int GuiOpenStore(void);
int GuiOpenDevices(void);
/* PR-GUI-tty-win：串口会话窗（单例） */
int GuiOpenTty(void);
/* PR-V2：打开/复用 Edit 窗；Path 为 FAT 文本路径；失败 -1 */
int GuiOpenEdit(const char *Path);
/* PR-G14：用户窗；成功返回 wid（槽位下标），失败 -1 */
int GuiOpenUser(const char *Title, UINT32 W, UINT32 H);
int GuiDamageUser(int Wid, const char *Text);
/* PR-G-desk-3：客户区相对坐标像素 blit（0x00RRGGBB）；成功 0 */
int GuiDamageRectUser(int Wid, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                      const UINT32 *Pixels);
/* PR-G15：用户窗按钮；ButtonId 0..3；成功 0 */
int GuiUserAddButton(int Wid, int ButtonId, const char *Label);
/* 0=无 / 1=关 / 100+id=按钮 / 300+HID=键 / 400+=客户区点 */
int GuiPollUserInput(int Wid);
/* 焦点为 GUI_WIN_USER 时由 FeedHid 调用 */
void GuiUserEnqueueKey(UINT8 HidKey);
/* 用户进程退出时收掉残留 USER 窗；返回关掉个数（S2.2：0 则 ExitUser 勿 ThemeScene） */
int GuiCloseAllUserWindows(void);
/* exit 拆页表前等待关窗 Closing 结束（NUC SMP） */
void GuiWaitNoWindowClosing(void);
/* PR-I18N2：按当前语言刷新窗标题并重绘 chrome */
void GuiRefreshTitles(void);
GUI_WIN_KIND GuiFocusKind(void);
GUI_WIN_KIND GuiWindowKind(int Idx);

/* PR-G2：每窗 Shell 输入行与提示符状态（随 GUI_WINDOW 移动） */
void GuiConsolePull(char *Line, int *Len, int *WaitPrompt);
void GuiConsolePush(const char *Line, int Len, int WaitPrompt);
void GuiConsoleSetWaitPrompt(int Idx, int WaitPrompt);
int GuiConsoleNeedsPrompt(void);
void GuiConsoleMarkPrompt(void);
int GuiConsoleHasDisplay(void);
/* 焦点不在 Shell 时：标记所有 Shell 下次获焦需补 prompt */
void GuiShellRequestPrompt(void);

#endif
