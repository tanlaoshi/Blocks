/*
 * Console.h — 交互式 Shell 接口
 *
 * 双通道输出：串口 + 帧缓冲文字。命令通过 gConsole.Register 动态注册。
 * 表在开机模块之前填好（桌面加载数据库时就会登记命令）。
 * InitializeConsole 会再填一次。填表之后即可写出，不必再等 Shell 自己的 Initialize。
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "BootTypes.h"

typedef struct {
    void (*Initialize)(void);
    void (*RegisterBuiltins)(void);
    /* 仅一级：无二级表时 Argv[0]=命令名，参数从 Argv[1] 起 */
    void (*Register)(const char *Name, const char *Help,
                     void (*Handler)(int Argc, char **Argv));
    /* 一级+二级：分发后 Handler 收到 Argv[0]=二级名，参数从 Argv[1] 起（已去掉一级） */
    void (*Register2)(const char *Level1, const char *Level2, const char *Help,
                      void (*Handler)(int Argc, char **Argv));
    /* 别名 → 一级正统名（如 exit → halt）；不占 CMD 槽 */
    void (*RegisterAlias)(const char *CanonicalLevel1, const char *Alias);
    /* 粘连别名 → 一级+二级（如 tcplisten → tcp listen） */
    void (*RegisterAliasLine)(const char *Alias, const char *Level1,
                              const char *Level2);
    /* 从 TOYOS.DB 加载 al.* 用户别名（须在命令表注册之后） */
    void (*UserAliasLoad)(void);
    void (*Write)(const char *Text);
    void (*WriteLen)(const char *Data, UINTN Len);
    void (*WriteHex32)(UINT32 Value);
    void (*WriteHex64)(UINT64 Value);
    void (*OnChar)(char C);
    void (*OnCharEx)(char C, int FromSerial);
    void (*OnEnter)(void);
    void (*OnEnterEx)(int FromSerial);
    void (*OnBackspace)(void);
    void (*OnBackspaceEx)(int FromSerial);
    /* PR-BOX-1：Shell ↑↓ 历史；Down=1 更新 / 0 更旧 */
    void (*OnHistArrow)(int Down, int FromSerial);
    void (*OnHistHid)(UINT8 Key, int FromSerial);
    int (*HistFeedAnsi)(char C, int FromSerial);
    void (*SerialFeedChar)(char C, int *SkipLf);
    void (*HistPushLine)(void);
    void (*HistApplyNavToLine)(void);
    void (*HistOnEdit)(void);
    int (*HistIsBusy)(void);
    int (*HistIgnoreRx)(void);
    void (*HistClearIgnoreRx)(void);
    void (*CancelInput)(void);
    /* 丢弃输入行缓冲（不擦屏）；listen 中断后防 Enter 重跑旧命令 */
    void (*DiscardInput)(void);
    /* 清零挂起计数并立刻画提示符（tcp listen 停止等） */
    void (*ForceResumePrompt)(void);
    void (*BindFocus)(void);
    /* PR-D3：新开 Shell 窗后打印欢迎语与提示符 */
    void (*OnShellOpened)(void);
    /* PR-G8：按窗下标重画 Shell 客户区（主题合成用，不 Raise） */
    void (*PaintShellWindow)(int Idx);
    /* PR-D5：ThemeApply/GuiRedraw 后立刻重画所有 Shell，避免等再点标题栏 */
    void (*RepaintShellWindows)(void);
    /* PR-I2 补：Shell 客户区滚轮（行缓冲 scrollback，可滚回） */
    void (*OnWheel)(INT8 Wheel);
    /* PR-GUI-shell-sb：Shell 客户区滚动条点选 */
    void (*OnClick)(UINT32 X, UINT32 Y);

    /* PR-G2：焦点切换时保存/恢复当前窗输入行（由 GuiFocusSave/Apply 调用） */
    void (*FocusSave)(void);
    void (*FocusLoad)(void);

    /* exec/runuser 成功后延迟提示符，进程退出时 ShowPrompt */
    void (*WaitPrompt)(void);
    void (*ShowPrompt)(void);
    /* 用户 ELF 占 stdin：WaitPrompt 或仍有活用户任务（防 GUI 清掉 WaitPrompt 后 shell 抢键） */
    int (*StdinUserHold)(void);
    /* 键盘 → 用户 read(0)；与串口 UART 一并在 StdinGetChar 取 */
    void (*StdinPut)(char C);
    char (*StdinGetChar)(void);
    void (*StdinFlush)(void);

    /* listen 等后台任务期间抑制 toyos>；Notify 只换行输出 */
    void (*SuspendPrompt)(void);
    void (*ResumePrompt)(void);
    int (*PromptSuspended)(void);
    void (*Notify)(const char *Text);

    /*
     * Shell 异步 Job（dhcp / store …）：Hold 后本窗不立刻 toyos>，
     * 其它 Shell 仍可输入；完成后 Release 在发起窗补提示符。
     */
    void (*JobHoldPrompt)(void);
    void (*JobReleasePrompt)(void);
    void (*WriteToJobShell)(const char *Text);
    int (*JobPromptPending)(void);
    int (*JobConsumeSkipAfterCommand)(void);
    /* 关 Shell 窗时丢弃该窗 scrollback 槽 */
    void (*SbWinForget)(int Idx);
    /* RaiseWindow 挪槽后同步 scrollback / Job 窗号 */
    void (*SbShiftRaise)(int Idx, int Top);
    void (*JobShiftRaise)(int Idx, int Top);

    /* Worker：iwl 后台 + 首轮 DHCP 结束后打一次 ready + toyos>；可重入 */
    void (*AnnounceBootReady)(void);
    /* PR-K-uart-shell：运行时日志 → RUNTIME.LOG；Shell log / log save */
    int (*RuntimeLogFlush)(void);
    void (*RuntimeLogShow)(void);
    int (*RuntimeLogSave)(const char *Path);

    /* PR-A9：virt 串口 Shell（轮询 HalSerial + HalTimerPoll） */
    void (*SerialRun)(void);
} CONSOLE_OPS;

extern CONSOLE_OPS gConsole;

/* 把各文件里的函数填进表。可重复调用。 */
void ConsolePublishAll(void);

#endif
