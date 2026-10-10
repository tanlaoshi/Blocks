/*
 * SyscallFile.c — K51：open/read/write/close 根文件（薄）
 *
 * 【初学者】
 * fd 16..19；只读 open 时整文件读进 4KiB 槽；写 open 在 close 时落盘。
 */
#include "SyscallFile.h"
#include "FatFile.h"
#include "PhysicalMemory.h"

#define FILE_BUF_MAX 4096u

typedef struct {
    int Used;
    int Writable;
    char Path[FAT_NAME_MAX];
    UINT8 *Buf;
    UINT32 Size;
    UINT32 Offset;
} FILE_SLOT;

static FILE_SLOT gFile[FD_FILE_COUNT];

static void CopyPath(char *Dst, const char *Src) {
    int i = 0;
    if (Src == 0) {
        Dst[0] = 0;
        return;
    }
    while (Src[i] && i < FAT_NAME_MAX - 1) {
        Dst[i] = Src[i];
        i++;
    }
    Dst[i] = 0;
}

static int SlotIndex(int Fd) {
    if (Fd < FD_FILE_BASE || Fd >= FD_FILE_BASE + FD_FILE_COUNT) {
        return -1;
    }
    return Fd - FD_FILE_BASE;
}

int SyscallFileIsFd(UINT64 Fd) {
    return SlotIndex((int)Fd) >= 0;
}

int SyscallFileOpen(const char *Path, UINT64 Flags) {
    int i;
    int Slot = -1;
    UINT32 Got = 0;

    if (Path == 0 || Path[0] == 0) {
        return -1;
    }
    for (i = 0; i < FD_FILE_COUNT; i++) {
        if (!gFile[i].Used) {
            Slot = i;
            break;
        }
    }
    if (Slot < 0) {
        return -1;
    }
    gFile[Slot].Buf = (UINT8 *)PhysicalMemoryAllocatePages(1);
    if (gFile[Slot].Buf == 0) {
        return -1;
    }
    gFile[Slot].Used = 1;
    gFile[Slot].Writable = (Flags != 0) ? 1 : 0;
    gFile[Slot].Offset = 0;
    gFile[Slot].Size = 0;
    CopyPath(gFile[Slot].Path, Path);

    if (!gFile[Slot].Writable) {
        if (FatFileReadPath(Path, gFile[Slot].Buf, FILE_BUF_MAX, &Got) < 0) {
            PhysicalMemoryFreePages(gFile[Slot].Buf, 1);
            gFile[Slot].Buf = 0;
            gFile[Slot].Used = 0;
            return -1;
        }
        gFile[Slot].Size = Got;
    }
    return FD_FILE_BASE + Slot;
}

int SyscallFileRead(int Fd, void *Buf, UINTN Len) {
    int Slot = SlotIndex(Fd);
    UINTN N;
    UINTN i;
    UINT8 *Dst;
    UINT8 *Src;

    if (Slot < 0 || !gFile[Slot].Used || gFile[Slot].Writable || Buf == 0) {
        return -1;
    }
    if (gFile[Slot].Offset >= gFile[Slot].Size) {
        return 0;
    }
    N = gFile[Slot].Size - gFile[Slot].Offset;
    if (N > Len) {
        N = Len;
    }
    Dst = (UINT8 *)Buf;
    Src = gFile[Slot].Buf + gFile[Slot].Offset;
    for (i = 0; i < N; i++) {
        Dst[i] = Src[i];
    }
    gFile[Slot].Offset += (UINT32)N;
    return (int)N;
}

int SyscallFileWrite(int Fd, const void *Buf, UINTN Len) {
    int Slot = SlotIndex(Fd);
    UINTN N;
    UINTN i;
    const UINT8 *Src;

    if (Slot < 0 || !gFile[Slot].Used || !gFile[Slot].Writable || Buf == 0) {
        return -1;
    }
    if (gFile[Slot].Size >= FILE_BUF_MAX) {
        return -1;
    }
    N = FILE_BUF_MAX - gFile[Slot].Size;
    if (N > Len) {
        N = Len;
    }
    Src = (const UINT8 *)Buf;
    for (i = 0; i < N; i++) {
        gFile[Slot].Buf[gFile[Slot].Size + i] = Src[i];
    }
    gFile[Slot].Size += (UINT32)N;
    return (int)N;
}

int SyscallFileClose(int Fd) {
    int Slot = SlotIndex(Fd);

    if (Slot < 0 || !gFile[Slot].Used) {
        return -1;
    }
    if (gFile[Slot].Writable) {
        if (FatFileWritePath(gFile[Slot].Path, gFile[Slot].Buf,
                             gFile[Slot].Size) != 0) {
            PhysicalMemoryFreePages(gFile[Slot].Buf, 1);
            gFile[Slot].Buf = 0;
            gFile[Slot].Used = 0;
            return -1;
        }
    }
    if (gFile[Slot].Buf != 0) {
        PhysicalMemoryFreePages(gFile[Slot].Buf, 1);
    }
    gFile[Slot].Buf = 0;
    gFile[Slot].Used = 0;
    gFile[Slot].Path[0] = 0;
    return 0;
}
