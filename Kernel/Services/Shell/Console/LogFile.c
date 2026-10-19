/*
 * LogFile.c — PR-K-uart-shell：运行时日志落盘 RUNTIME.LOG
 *
 * ShellOwn 后 ToyLog 只进缓冲；Worker 脏时刷盘；Shell log / log save 可拉。
 */
#include "Console.h"
#include "LogFile.h"
#include "HalSerial.h"
#include "FileSystem.h"
#include "Fat.h"
#include "ToySerialConfig.h"

#define RT_PATH "RUNTIME.LOG"
#define RT_SNAP 16384u

static char gSnap[RT_SNAP];

static int RuntimeLogFlush(void) {
    UINTN N;
    int Err;

    if (!HalSerialShellOwned() || !HalSerialRuntimeDirty()) {
        return 0;
    }
    N = HalSerialRuntimeSnapshot(gSnap, RT_SNAP);
    if (N == 0) {
        HalSerialRuntimeMarkSaved();
        return 0;
    }
    Err = FileSystemWriteFile(RT_PATH, gSnap, N);
    if (Err != FAT_OK) {
        return -1;
    }
    HalSerialRuntimeMarkSaved();
    return 0;
}

static void RuntimeLogShow(void) {
    UINTN N;
    UINTN i;
    char Chunk[129];
    UINTN C;

    N = HalSerialRuntimeSnapshot(gSnap, RT_SNAP);
    if (N == 0) {
        gConsole.Write("(runtime log empty — post-ready ToyLog only)\n");
        return;
    }
    i = 0;
    while (i < N) {
        C = 0;
        while (i < N && C + 1 < sizeof(Chunk)) {
            Chunk[C++] = gSnap[i++];
        }
        Chunk[C] = 0;
        gConsole.Write(Chunk);
    }
    if (N > 0 && gSnap[N - 1] != '\n') {
        gConsole.Write("\n");
    }
}

static int RuntimeLogSave(const char *Path) {
    UINTN N;
    int Err;

    if (!Path || !Path[0]) {
        Path = RT_PATH;
    }
    N = HalSerialRuntimeSnapshot(gSnap, RT_SNAP);
    Err = FileSystemWriteFile(Path, gSnap, N);
    if (Err != FAT_OK) {
        return -1;
    }
    HalSerialRuntimeMarkSaved();
    return 0;
}

#if TOY_FILE_LOG
#define BOOT_LOG_MAX (64u * 1024u)
static char gBootSnap[BOOT_LOG_MAX];
#endif

void BootLogFlush(void) {
#if !TOY_FILE_LOG
    return;
#else
    static const char *Paths[] = { "BOOT.LOG", "USB:BOOT.LOG", "BLOCKS:BOOT.LOG" };
    UINTN N;
    int i;

    N = HalSerialBootSnapshot(gBootSnap, BOOT_LOG_MAX);
    if (N == 0) {
        return;
    }
    for (i = 0; i < (int)(sizeof(Paths) / sizeof(Paths[0])); i++) {
        (void)FileSystemWriteFile(Paths[i], gBootSnap, N);
    }
#endif
}

void ConsolePublishLog(CONSOLE_OPS *Ops) {
    Ops->RuntimeLogFlush = RuntimeLogFlush;
    Ops->RuntimeLogShow = RuntimeLogShow;
    Ops->RuntimeLogSave = RuntimeLogSave;
}
