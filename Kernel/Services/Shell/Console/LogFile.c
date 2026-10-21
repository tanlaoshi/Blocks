/*
 * LogFile.c — 运行时 / 开机日志落盘
 *
 * 写到每个已挂 BLOCKS 卷，文件名区分本盘角色（便于 NUC / U 盘两份对照）：
 *   BOOT-BOOTDISK.LOG / RUNTIME-BOOTDISK.LOG — 本卷在「加载 EFI 的那块盘」上
 *   BOOT-OTHER.LOG    / RUNTIME-OTHER.LOG    — 旁路盘上的副本
 * NUC 起：NUC 的 BLOCKS 上是 *-BOOTDISK；U 盘起后 U 盘 BLOCKS 上是 *-BOOTDISK。
 */
#include "Console.h"
#include "LogFile.h"
#include "HalSerial.h"
#include "FileSystem.h"
#include "Fat.h"
#include "SerialConfig.h"

#define RT_SNAP 16384u

static char gSnap[RT_SNAP];

/* Path = "A:BOOT-BOOTDISK.LOG" 等；成功写过任一卷则 1 */
static int WriteToBlocksVols(const char *NameBootDisk, const char *NameOther,
                             const char *Data, UINTN N) {
    int i;
    int Wrote = 0;
    char Path[48];
    const char *Name;
    int OnBoot;
    UINTN Len;
    UINTN j;

    if (!Data || N == 0 || !NameBootDisk || !NameOther) {
        return 0;
    }
    for (i = 0; i < FileSystemVolCount(); i++) {
        if (!FileSystemVolHasBlocksId(i)) {
            continue;
        }
        OnBoot = FileSystemVolOnBootDisk(i);
        if (OnBoot == 1) {
            Name = NameBootDisk;
        } else {
            Name = NameOther;
        }
        Path[0] = (char)('A' + i);
        Path[1] = ':';
        Len = 2;
        for (j = 0; Name[j] && Len + 1 < sizeof(Path); j++) {
            Path[Len++] = Name[j];
        }
        Path[Len] = 0;
        if (FileSystemWriteFile(Path, Data, N) == FAT_OK) {
            Wrote = 1;
        }
    }
    return Wrote;
}

static int RuntimeLogFlush(void) {
    UINTN N;

    if (!HalSerialShellOwned() || !HalSerialRuntimeDirty()) {
        return 0;
    }
    N = HalSerialRuntimeSnapshot(gSnap, RT_SNAP);
    if (N == 0) {
        HalSerialRuntimeMarkSaved();
        return 0;
    }
    if (!WriteToBlocksVols("RUNTIME-BOOTDISK.LOG", "RUNTIME-OTHER.LOG", gSnap, N)) {
        /* 尚无 BLOCKS 卷时回退默认路径 */
        if (FileSystemWriteFile("RUNTIME.LOG", gSnap, N) != FAT_OK) {
            return -1;
        }
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
        gConsole.Write("(runtime log empty — post-ready Log only)\n");
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
        Path = "RUNTIME-BOOTDISK.LOG";
    }
    N = HalSerialRuntimeSnapshot(gSnap, RT_SNAP);
    Err = FileSystemWriteFile(Path, gSnap, N);
    if (Err != FAT_OK) {
        return -1;
    }
    HalSerialRuntimeMarkSaved();
    return 0;
}

#if FILE_LOG
#define BOOT_LOG_MAX (64u * 1024u)
static char gBootSnap[BOOT_LOG_MAX];
#endif

void BootLogFlush(void) {
#if !FILE_LOG
    return;
#else
    UINTN N;

    N = HalSerialBootSnapshot(gBootSnap, BOOT_LOG_MAX);
    if (N == 0) {
        return;
    }
    if (!WriteToBlocksVols("BOOT-BOOTDISK.LOG", "BOOT-OTHER.LOG", gBootSnap, N)) {
        (void)FileSystemWriteFile("BOOT.LOG", gBootSnap, N);
        (void)FileSystemWriteFile("BLOCKS:BOOT.LOG", gBootSnap, N);
    }
#endif
}

void ConsolePublishLog(CONSOLE_OPS *Ops) {
    Ops->RuntimeLogFlush = RuntimeLogFlush;
    Ops->RuntimeLogShow = RuntimeLogShow;
    Ops->RuntimeLogSave = RuntimeLogSave;
}
