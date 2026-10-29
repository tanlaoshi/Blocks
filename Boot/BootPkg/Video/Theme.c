/*
 * Theme.c — 显示偏好：从 BLOCKS.DB 读 mode=WxH（分辨率唯一真相）
 *           mode= 只认第一行；mode=auto 表示「不指定目标」，交给 EDID/最高模式兜底。
 */
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

/*
 * mode= 行的解析结果。用三态而不是布尔：把「不是 mode= 行（含写错）」
 * 与「显式写了 mode=auto」分开，调用方不必再靠 W/H 的数值去猜意图。
 */
typedef enum {
    MODE_LINE_NONE = 0, /* 不是 mode= 行，或格式不对：继续看下一行 */
    MODE_LINE_AUTO,     /* mode=auto：显式要求自动选模，不指定目标 */
    MODE_LINE_EXACT     /* mode=WxH：显式目标分辨率 */
} MODE_LINE_KIND;

/* 解析一行 mode=WxH 或 mode=auto */
STATIC MODE_LINE_KIND ParseAsciiModeLine(const CHAR8 *Line, UINT32 *OutW, UINT32 *OutH) {
    UINT32 W = 0;
    UINT32 H = 0;
    UINTN i = 0;

    if (Line == NULL || OutW == NULL || OutH == NULL) {
        return MODE_LINE_NONE;
    }
    while (Line[i] == ' ' || Line[i] == '\t') {
        i++;
    }
    if (Line[i] == 'm' && Line[i + 1] == 'o' && Line[i + 2] == 'd' &&
        Line[i + 3] == 'e' && Line[i + 4] == '=') {
        i += 5;
    } else {
        return MODE_LINE_NONE;
    }
    if (Line[i] == 'a' || Line[i] == 'A') {
        *OutW = 0;
        *OutH = 0;
        return MODE_LINE_AUTO;
    }
    while (Line[i] >= '0' && Line[i] <= '9') {
        W = W * 10 + (UINT32)(Line[i] - '0');
        i++;
        if (W > 10000) {
            return MODE_LINE_NONE;
        }
    }
    if (Line[i] != 'x' && Line[i] != 'X') {
        return MODE_LINE_NONE;
    }
    i++;
    while (Line[i] >= '0' && Line[i] <= '9') {
        H = H * 10 + (UINT32)(Line[i] - '0');
        i++;
        if (H > 10000) {
            return MODE_LINE_NONE;
        }
    }
    if (W < 640 || H < 480) {
        return MODE_LINE_NONE;
    }
    *OutW = W;
    *OutH = H;
    return MODE_LINE_EXACT;
}

STATIC BOOLEAN ParseModeFromDb(const CHAR8 *Buf, UINTN Size, UINT32 *OutW,
                               UINT32 *OutH) {
    UINTN i;
    CHAR8 Line[64];
    UINTN L = 0;
    MODE_LINE_KIND Kind;

    if (Buf == NULL || Size == 0) {
        return FALSE;
    }
    for (i = 0; i <= Size; i++) {
        CHAR8 C = (i < Size) ? Buf[i] : '\n';
        if (C == '\n' || C == '\r' || i == Size) {
            if (L > 0) {
                Line[L] = 0;
                Kind = ParseAsciiModeLine(Line, OutW, OutH);
                L = 0;
                /*
                 * 首行生效：mode= 只认第一条。
                 * auto = 显式「不指定目标」，结束并回报无目标（由 EDID/最高模式兜底）；
                 * 格式不对 = MODE_LINE_NONE，继续往下找，别让一行笔误吃掉整份配置。
                 */
                if (Kind == MODE_LINE_AUTO) {
                    return FALSE;
                }
                if (Kind == MODE_LINE_EXACT) {
                    return TRUE;
                }
            }
            continue;
        }
        if (L + 1 < sizeof(Line)) {
            Line[L++] = C;
        }
    }
    return FALSE;
}

STATIC EFI_STATUS ReadDbFileOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                 CHAR8 **OutBuf, UINTN *OutSize) {
    STATIC CHAR16 *Paths[] = {
        L"\\BLOCKS.DB",
    };
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN InfoSize;
    UINTN p;
    CHAR8 *Buf = NULL;
    UINTN ReadSize;

    *OutBuf = NULL;
    *OutSize = 0;

    Status = Fs->OpenVolume(Fs, &Root);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    for (p = 0; p < sizeof(Paths) / sizeof(Paths[0]); p++) {
        Status = Root->Open(Root, &File, Paths[p], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(Status)) {
            break;
        }
        File = NULL;
    }
    if (File == NULL) {
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }

    InfoSize = sizeof(EFI_FILE_INFO) + 128;
    Status = gBS->AllocatePool(EfiLoaderData, InfoSize, (VOID **)&FileInfo);
    if (EFI_ERROR(Status)) {
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    Status = File->GetInfo(File, &gEfiFileInfoGuid, &InfoSize, FileInfo);
    if (EFI_ERROR(Status) || FileInfo->FileSize == 0 || FileInfo->FileSize > 8192) {
        gBS->FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return EFI_NOT_FOUND;
    }

    Status = gBS->AllocatePool(EfiLoaderData, (UINTN)FileInfo->FileSize + 1,
                               (VOID **)&Buf);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(FileInfo);
        File->Close(File);
        Root->Close(Root);
        return Status;
    }

    ReadSize = (UINTN)FileInfo->FileSize;
    Status = File->Read(File, &ReadSize, Buf);
    gBS->FreePool(FileInfo);
    File->Close(File);
    Root->Close(Root);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(Buf);
        return Status;
    }
    Buf[ReadSize] = 0;
    *OutBuf = Buf;
    *OutSize = ReadSize;
    return EFI_SUCCESS;
}

STATIC BOOLEAN TryParseModeOnFs(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs,
                                UINT32 *OutW, UINT32 *OutH) {
    CHAR8 *Buf = NULL;
    UINTN Size = 0;

    if (EFI_ERROR(ReadDbFileOnFs(Fs, &Buf, &Size))) {
        return FALSE;
    }
    if (ParseModeFromDb(Buf, Size, OutW, OutH)) {
        gBS->FreePool(Buf);
        return TRUE;
    }
    gBS->FreePool(Buf);
    return FALSE;
}

BOOLEAN TryLoadDisplayPref(EFI_HANDLE ImageHandle, UINT32 *OutW,
                                  UINT32 *OutH) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN i;
    UINTN Pass;

    Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
                                 (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        return FALSE;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                     NULL, &HandleCount, &Handles);
    if (EFI_ERROR(Status)) {
        Handles = NULL;
        HandleCount = 0;
    }

    /*
     * boot-root-cfg：与 ReadKernel 同序（ROOT.CFG → 同盘 ID → 它盘 ID → 启动卷）。
     */
    for (Pass = 0; Pass < 4; Pass++) {
        for (i = 0; i < HandleCount; i++) {
            Status = gBS->HandleProtocol(Handles[i], &gEfiSimpleFileSystemProtocolGuid,
                                         (VOID **)&Fs);
            if (EFI_ERROR(Status)) {
                continue;
            }
            if (!BootOsVolForPass(ImageHandle, Handles[i], Pass)) {
                continue;
            }
            if (TryParseModeOnFs(Fs, OutW, OutH)) {
                gBS->FreePool(Handles);
                return TRUE;
            }
        }
    }

    if (Handles != NULL) {
        gBS->FreePool(Handles);
    }
    (void)LoadedImage;
    return FALSE;
}
