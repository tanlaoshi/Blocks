/*
 * CoreOps.h — PR-R4：Core 经 ops 表调 Services（不再直 #include Gui/FileSystem）
 *
 * WindowOps：用户窗协议（Syscall）
 * VfsServiceOps：多卷 FileSystem* 门面（Process / TaskFd）；与 Library gVirtualFileSystem.Ops/FS_OPS 不同层
 */
#ifndef CORE_OPS_H
#define CORE_OPS_H

#include "BootTypes.h"
#include "Fat.h"

typedef struct {
    int (*OpenUser)(const char *Title, UINT32 W, UINT32 H);
    int (*DamageUser)(int Wid, const char *Text);
    int (*DamageRectUser)(int Wid, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                          const UINT32 *Pixels);
    int (*PollUserInput)(int Wid);
    int (*AddButton)(int Wid, int ButtonId, const char *Label);
} WINDOW_OPS;

void WindowOpsRegister(const WINDOW_OPS *Ops);
int WindowOpenUser(const char *Title, UINT32 W, UINT32 H);
int WindowDamageUser(int Wid, const char *Text);
int WindowDamageRectUser(int Wid, UINT32 X, UINT32 Y, UINT32 W, UINT32 H,
                         const UINT32 *Pixels);
int WindowPollUserInput(int Wid);
int WindowAddButton(int Wid, int ButtonId, const char *Label);

typedef struct {
    int (*ReadFile)(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
    int (*WriteFile)(const char *Path, const void *Buffer, UINTN Size);
    int (*ListEntries)(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
    int (*FileStat)(const char *Path, FAT_FILE_STAT *Out);
    /* PR-U-stream-1；可为 NULL */
    int (*ReadFileAt)(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
    int (*WriteFileAt)(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);
} VFS_SERVICE_OPS;

void VfsServiceOpsRegister(const VFS_SERVICE_OPS *Ops);
int VfsServiceReadFile(const char *Path, void *Buffer, UINTN MaxSize, UINTN *OutSize);
int VfsServiceWriteFile(const char *Path, const void *Buffer, UINTN Size);
int VfsServiceListEntries(const char *Path, FAT_DIRECTORY_ENTRY *Out, int Max, int *OutCount);
int VfsServiceFileStat(const char *Path, FAT_FILE_STAT *Out);
int VfsServiceReadFileAt(const char *Path, UINTN Offset, void *Buffer, UINTN Len, UINTN *OutN);
int VfsServiceWriteFileAt(const char *Path, UINTN Offset, const void *Buffer, UINTN Len, UINTN *OutN);

/* 标准输出按长度写、标准输入取一字。控制台启动时登记；没登记则写出走串口，读入失败。 */
typedef struct {
    void (*WriteLen)(const char *Data, UINTN Len);
    char (*ReadChar)(void);
    void (*ShowPrompt)(void);
} CONSOLE_IO_OPS;

void ConsoleIoRegister(const CONSOLE_IO_OPS *Ops);
void ConsoleIoWriteLen(const char *Data, UINTN Len);
int ConsoleIoReadChar(char *Out);
void ConsoleIoShowPrompt(void);

/* 长 IO 呼吸和用户进程退出时的桌面动作。Gui 初始化时登记；没登记则这些调用什么都不做。 */
typedef struct {
    void (*PollMouseMotion)(void);
    void (*CloseAllUserWindows)(void);
    void (*WaitNoWindowClosing)(void);
    void (*ComposeThemeScene)(void);
} DESKTOP_OPS;

void DesktopOpsRegister(const DESKTOP_OPS *Ops);
void DesktopPollMouseMotion(void);
void DesktopCloseAllUserWindows(void);
void DesktopWaitNoWindowClosing(void);
void DesktopComposeThemeScene(void);

/* 换字。Theme 初始化时登记；没登记则只改 Library 里的当前字。 */
void FontChoiceRegister(int (*Set)(UINT32 Id));
int FontChoiceSet(UINT32 Id);

#endif
