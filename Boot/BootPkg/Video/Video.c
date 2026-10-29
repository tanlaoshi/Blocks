/*
 * Video.c — Boot 第 2 步：GOP 选模（GetVideoInfo）/ 设模（SetVideoMode）
 */
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

#include "BootPrivate.h"

STATIC EFI_STATUS FailVideo(CONST CHAR8 *Step, EFI_STATUS Status) {
    BootSerialPrintf("Boot: %a Failed: %r\n", Step, Status);
    return Status;
}

/* Get → Set 之间的选模结果，由调用方持有（原先 13 个文件级 static） */
EFI_STATUS GetVideoInfo(EFI_HANDLE ImageHandle, UEFI_BOOT_CONFIG *BootConfig,
                        VIDEO_SELECTION *Sel) {
    EFI_STATUS                            Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop = NULL;
    UINTN                                 HandleCount = 0;
    EFI_HANDLE                            *HandleBuffer = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo = NULL;
    UINTN                                 InfoSize = 0;
    UINTN                                 BestMode = 0;
    UINTN                                 BestScore = 0;
    BOOLEAN                               KeepCurrentMode = FALSE;
    BOOLEAN                               FoundMode = FALSE;
    UINT32                                BestW = 0;
    UINT32                                BestH = 0;
    BOOLEAN                               InVm = IsVirtualMachine();
    BOOLEAN                               HasEdidTarget = FALSE;
    UINT32                                EdidW = 0;
    UINT32                                EdidH = 0;
    BOOLEAN                               HasCfgTarget = FALSE;
    UINT32                                CfgW = 0;
    UINT32                                CfgH = 0;
    BOOLEAN                               CfgMatched = FALSE;
    UINT32                                ModeCount = 0;

    BootSerialPrintf("Boot: GetVideoInfo\n");

    if (Sel == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    ZeroMem(Sel, sizeof(*Sel));

    if (BootConfig != NULL) {
        BootConfig->VideoModeCount = 0;
        BootConfig->VideoModePad = 0;
        BootConfig->GopProtocol = 0;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                     NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR(Status)) {
        return FailVideo("GetVideoInfo", Status);
    }

    Status = gBS->OpenProtocol(HandleBuffer[0], &gEfiGraphicsOutputProtocolGuid,
                               (VOID **)&Gop, ImageHandle, NULL,
                               EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(HandleBuffer);
        return FailVideo("GetVideoInfo", Status);
    }

    if (TryLoadDisplayPref(ImageHandle, &CfgW, &CfgH)) {
        HasCfgTarget = TRUE;
        BootDbg("Boot: BLOCKS.DB Mode %dx%d\n", CfgW, CfgH);
    }

    if (!InVm) {
        if (!EFI_ERROR(TryGetEdidPreferred(ImageHandle, HandleBuffer[0], &EdidW, &EdidH))) {
            HasEdidTarget = TRUE;
            BootDbg("Boot: Monitor EDID Preferred %dx%d\n", EdidW, EdidH);
        } else {
            BootSerialPrintf("Boot: EDID Unavailable, Using Highest GOP Mode\n");
        }
    } else {
        BootDbg("Boot: Virtual Machine Detected, Using QEMU-Friendly Mode Table\n");
    }

    /*
     * 若无 mode= 时优先沿用固件当前可用模式。
     * 勿为「EDID 原生 / 最高像素」再 SetMode（4K 切模贵）；有 mode= 仍尊重。
     */
    {
        UINT32 CurW = 0;
        UINT32 CurH = 0;

        if (Gop->Mode != NULL && Gop->Mode->Info != NULL) {
            CurW = Gop->Mode->Info->HorizontalResolution;
            CurH = Gop->Mode->Info->VerticalResolution;
        }
        if (!HasCfgTarget && CurW >= 640 && CurH >= 480) {
            BestMode = (UINTN)Gop->Mode->Mode;
            BestW = CurW;
            BestH = CurH;
            /* 开关不再是「一个特别大的分值」：分值域里只有真实得分 */
            KeepCurrentMode = TRUE;
            FoundMode = TRUE;
            BestScore = 0;
            BootDbg("Boot: Keep Firmware Mode %dx%d (No mode=)\n", CurW, CurH);
        }
    }

    BootDbg("Available Video Modes:\n");
    for (UINTN i = 0; i < Gop->Mode->MaxMode; i++) {
        Status = Gop->QueryMode(Gop, i, &InfoSize, &ModeInfo);
        if (EFI_ERROR(Status) || ModeInfo == NULL) {
            continue;
        }
        if (!IsModeUsable(ModeInfo)) {
            gBS->FreePool(ModeInfo);
            ModeInfo = NULL;
            continue;
        }

        {
            UINT32 W = ModeInfo->HorizontalResolution;
            UINT32 H = ModeInfo->VerticalResolution;
            UINTN  Score;
            UINT32 Mi;

            if (BootConfig != NULL && ModeCount < UEFI_VIDEO_MODE_MAX) {
                for (Mi = 0; Mi < ModeCount; Mi++) {
                    if (BootConfig->VideoModes[Mi].Width == W &&
                        BootConfig->VideoModes[Mi].Height == H) {
                        break;
                    }
                }
                if (Mi == ModeCount) {
                    BootConfig->VideoModes[ModeCount].Width = W;
                    BootConfig->VideoModes[ModeCount].Height = H;
                    BootConfig->VideoModes[ModeCount].ModeNumber = (UINT32)i;
                    BootConfig->VideoModes[ModeCount].Reserved = 0;
                    ModeCount++;
                }
            }

            if (HasCfgTarget && W == CfgW && H == CfgH) {
                Score = SCORE_CFG_EXACT;
                CfgMatched = TRUE;
            } else if (HasCfgTarget) {
                Score = ScoreModeNative(W, H, CfgW, CfgH, TRUE);
                /*
                 * 请求的宽高比不在 GOP 里时，跟显示器的宽高比。
                 * 否则 1280x800 会就近落到 1280x1024，再被 1080p 屏拉扁。
                 */
                if (HasEdidTarget &&
                    !SameAspectRatio(W, H, CfgW, CfgH) &&
                    SameAspectRatio(W, H, EdidW, EdidH) &&
                    Score < SCORE_ASPECT_FALLBACK) {
                    Score = SCORE_ASPECT_FALLBACK +
                            ((UINTN)W * (UINTN)H) / SCORE_AREA_DIV;
                }
            } else if (KeepCurrentMode) {
                Score = 0;
            } else if (InVm) {
                Score = ScoreModeQemu(W, H);
            } else {
                Score = ScoreModeNative(W, H, EdidW, EdidH, HasEdidTarget);
            }

            BootDbg("  Mode %d: %dx%d (Score: %d)\n", i, W, H, Score);

            if (!KeepCurrentMode && Score > BestScore) {
                BestScore = Score;
                BestMode = i;
                BestW = W;
                BestH = H;
                FoundMode = TRUE;
            }
        }

        gBS->FreePool(ModeInfo);
        ModeInfo = NULL;
        InfoSize = 0;
    }

    if (BootConfig != NULL) {
        SortVideoModesForSettings(BootConfig->VideoModes, ModeCount,
                                  InVm, HasEdidTarget, EdidW, EdidH);
        BootConfig->VideoModeCount = ModeCount;
        BootDbg("Boot: %u Unique GOP Modes For Settings", ModeCount);
        if (!InVm && HasEdidTarget) {
            BootDbg(" (List: EDID %dx%d First)\n", EdidW, EdidH);
        } else {
            BootDbg("\n");
        }
    }

    if (HasCfgTarget && !CfgMatched) {
        if (SameAspectRatio(BestW, BestH, CfgW, CfgH)) {
            BootDbg("Boot: Mode %dx%d Not In GOP; Nearest Same-Aspect %dx%d\n",
                  CfgW, CfgH, BestW, BestH);
        } else {
            BootDbg("Boot: Mode %dx%d Not In GOP; Nearest %dx%d\n",
                  CfgW, CfgH, BestW, BestH);
        }
    } else if (!HasCfgTarget && HasEdidTarget &&
               (BestW != EdidW || BestH != EdidH)) {
        if (SameAspectRatio(BestW, BestH, EdidW, EdidH)) {
            BootDbg("Boot: EDID %dx%d Not In GOP; Nearest Same-Aspect %dx%d\n",
                  EdidW, EdidH, BestW, BestH);
        } else {
            BootDbg("Boot: EDID %dx%d Not In GOP; Nearest %dx%d\n",
                  EdidW, EdidH, BestW, BestH);
        }
    }

    gBS->FreePool(HandleBuffer);

    /* 用独立开关判断「有没有选到」，而不是拿分值为 0 猜 */
    if (!FoundMode) {
        BootDbg("Boot: No Usable GOP Mode Found\n");
        return FailVideo("GetVideoInfo", EFI_NOT_FOUND);
    }

    Sel->Gop = Gop;
    Sel->BestMode = BestMode;
    Sel->BestW = BestW;
    Sel->BestH = BestH;
    Sel->BestScore = BestScore;
    Sel->InVm = InVm;
    Sel->HasCfgTarget = HasCfgTarget;
    Sel->CfgMatched = CfgMatched;
    Sel->HasEdidTarget = HasEdidTarget;
    Sel->CfgW = CfgW;
    Sel->CfgH = CfgH;
    Sel->EdidW = EdidW;
    Sel->EdidH = EdidH;
    Sel->ModeCount = ModeCount;

    BootSerialPrintf("Boot: GetVideoInfo Done (Pick %dx%d Mode %d)\n",
                     BestW, BestH, (UINT32)BestMode);
    return EFI_SUCCESS;
}

EFI_STATUS SetVideoMode(EFI_HANDLE ImageHandle, VIDEO_CONFIG *VideoConfig,
                        UEFI_BOOT_CONFIG *BootConfig,
                        const VIDEO_SELECTION *Sel) {
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;

    (void)ImageHandle;

    BootSerialPrintf("Boot: SetVideoMode\n");

    /* 没有先跑 GetVideoInfo（或上下文为空）就是调用顺序错误 */
    if (Sel == NULL || Sel->Gop == NULL || VideoConfig == NULL) {
        return FailVideo("SetVideoMode", EFI_NOT_READY);
    }
    Gop = Sel->Gop;

    /* 已是目标分辨率则勿 SetMode：QEMU+GTK 下改分辨率常会整机再复位一次 */
    {
        UINT32 CurW = 0;
        UINT32 CurH = 0;

        if (Gop->Mode != NULL && Gop->Mode->Info != NULL) {
            CurW = Gop->Mode->Info->HorizontalResolution;
            CurH = Gop->Mode->Info->VerticalResolution;
        }

        if (CurW == Sel->BestW && CurH == Sel->BestH) {
            BootSerialPrintf("Boot: Already %dx%d, Skip SetMode\n", Sel->BestW, Sel->BestH);
        } else if (Sel->InVm) {
            /*
             * Guest reboot / QEMU Reset 不会重读宿主 run.sh 的 edid；若此处 SetMode
             * 改分辨率，GTK 跳变会再复位，固件又回到 edid 旧模式 → 无限重启。
             */
            BootDbg("Boot: Skip SetMode %dx%d -> %dx%d On VM (QEMU+GTK Loop)\n",
                  CurW, CurH, Sel->BestW, Sel->BestH);
            if (Sel->HasCfgTarget) {
                BootDbg("Boot: BLOCKS.DB Wants %dx%d But GOP Is %dx%d\n",
                      Sel->CfgW, Sel->CfgH, CurW, CurH);
                BootDbg("Boot: Quit QEMU Window, Then Runtime/run.sh (EDID From BLOCKS.DB)\n");
            }
        } else {
            Status = Gop->SetMode(Gop, Sel->BestMode);
            if (EFI_ERROR(Status)) {
                BootSerialPrintf("Boot: SetMode(%d) Failed: %r\n", Sel->BestMode, Status);
                return Status;
            }
        }
    }

    VideoConfig->FrameBufferBase = Gop->Mode->FrameBufferBase;
    VideoConfig->FrameBufferSize = Gop->Mode->FrameBufferSize;
    VideoConfig->HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    VideoConfig->VerticalResolution = Gop->Mode->Info->VerticalResolution;
    VideoConfig->PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    if (Sel->HasCfgTarget && Sel->CfgMatched &&
        VideoConfig->HorizontalResolution == Sel->CfgW &&
        VideoConfig->VerticalResolution == Sel->CfgH) {
        BootDbg("Boot: Display %dx%d (BLOCKS.DB)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    } else if (Sel->HasCfgTarget &&
               (VideoConfig->HorizontalResolution != Sel->CfgW ||
                VideoConfig->VerticalResolution != Sel->CfgH)) {
        if (Sel->InVm) {
            BootDbg("Boot: Display %dx%d (GOP; BLOCKS.DB %dx%d Not Applied — Relaunch QEMU)\n",
                  VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution,
                  Sel->CfgW, Sel->CfgH);
        } else {
            BootDbg("Boot: Display %dx%d (Nearest To BLOCKS.DB %dx%d)\n",
                  VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution,
                  Sel->CfgW, Sel->CfgH);
        }
    } else if (Sel->InVm) {
        BootDbg("Boot: Display %dx%d (QEMU/VM)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    } else if (Sel->HasEdidTarget &&
               VideoConfig->HorizontalResolution == Sel->EdidW &&
               VideoConfig->VerticalResolution == Sel->EdidH) {
        BootDbg("Boot: Display %dx%d (EDID Native)\n", Sel->EdidW, Sel->EdidH);
    } else {
        BootDbg("Boot: Display %dx%d (Hardware Best Match)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    }

    BootDbg("Selected Mode: %d, Final %dx%d\n",
          Sel->BestMode, VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);

    if (BootConfig != NULL) {
        BootConfig->VideoModeCount = Sel->ModeCount;
        BootConfig->GopProtocol = (UINT64)(UINTN)Gop;
        BootConfig->VideoModePad = UEFI_GOP_HANDOFF_MAGIC;
    }

    if (Gop->Mode != NULL && Gop->Mode->Info != NULL) {
        /* 与内核 Logo 底色一致（纯黑）；Boot 只清屏，格子由内核画 */
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL Bg;
        UINTN W = Gop->Mode->Info->HorizontalResolution;
        UINTN H = Gop->Mode->Info->VerticalResolution;

        Bg.Red = 0;
        Bg.Green = 0;
        Bg.Blue = 0;
        Bg.Reserved = 0;
        (void)Gop->Blt(Gop, &Bg, EfiBltVideoFill, 0, 0, 0, 0, W, H, 0);
        BootSerialPrintf("Boot: GOP Cleared %ux%u\n", (UINT32)W, (UINT32)H);
    }

    BootSerialPrintf("Boot: SetVideoMode Done\n");
    return EFI_SUCCESS;
}
