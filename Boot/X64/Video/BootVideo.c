/*
 * BootVideo.c — GOP：GetVideoInfo 选模 / SetVideoMode 设分辨率
 */
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "BootPrivate.h"

/* Get → Set 之间暂存（UefiMain 单线程顺序调用） */
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *sGop;
STATIC UINTN                         sBestMode;
STATIC UINT32                        sBestW;
STATIC UINT32                        sBestH;
STATIC UINTN                         sBestScore;
STATIC BOOLEAN                       sInVm;
STATIC BOOLEAN                       sHasCfgTarget;
STATIC BOOLEAN                       sCfgMatched;
STATIC BOOLEAN                       sHasEdidTarget;
STATIC UINT32                        sCfgW;
STATIC UINT32                        sCfgH;
STATIC UINT32                        sEdidW;
STATIC UINT32                        sEdidH;
STATIC UINT32                        sModeCount;

EFI_STATUS GetVideoInfo(EFI_HANDLE ImageHandle, X64_BOOT_CONFIG *BootConfig) {
    EFI_STATUS                            Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL          *Gop = NULL;
    UINTN                                 HandleCount = 0;
    EFI_HANDLE                            *HandleBuffer = NULL;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo = NULL;
    UINTN                                 InfoSize = 0;
    UINTN                                 BestMode = 0;
    UINTN                                 BestScore = 0;
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

    sGop = NULL;
    sBestScore = 0;
    sModeCount = 0;

    if (BootConfig != NULL) {
        BootConfig->VideoModeCount = 0;
        BootConfig->VideoModePad = 0;
        BootConfig->GopProtocol = 0;
    }

    Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                     NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = gBS->OpenProtocol(HandleBuffer[0], &gEfiGraphicsOutputProtocolGuid,
                               (VOID **)&Gop, ImageHandle, NULL,
                               EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status)) {
        gBS->FreePool(HandleBuffer);
        return Status;
    }

    if (TryLoadDisplayPref(ImageHandle, &CfgW, &CfgH)) {
        HasCfgTarget = TRUE;
        BootDbg("ToyBoot: THEME.CFG Mode %dx%d\n", CfgW, CfgH);
    }

    if (!InVm) {
        if (!EFI_ERROR(TryGetEdidPreferred(ImageHandle, HandleBuffer[0], &EdidW, &EdidH))) {
            HasEdidTarget = TRUE;
            BootDbg("ToyBoot: Monitor EDID Preferred %dx%d\n", EdidW, EdidH);
        } else {
            BootSerialPrintf("ToyBoot: EDID Unavailable, Using Highest GOP Mode\n");
        }
    } else {
        BootDbg("ToyBoot: Virtual Machine Detected, Using QEMU-Friendly Mode Table\n");
    }

    /*
     * PR-BOOT-fast-4：无 mode= 时优先沿用固件当前可用模式。
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
            BestScore = 6000000000ULL;
            BootDbg("ToyBoot: Keep Firmware Mode %dx%d (No mode=)\n", CurW, CurH);
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

            if (BootConfig != NULL && ModeCount < TOY_VIDEO_MODE_MAX) {
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
                Score = 5000000000ULL;
                CfgMatched = TRUE;
            } else if (HasCfgTarget) {
                Score = ScoreModeNative(W, H, CfgW, CfgH, TRUE);
            } else if (BestScore >= 6000000000ULL) {
                Score = 0;
            } else if (InVm) {
                Score = ScoreModeQemu(W, H);
            } else {
                Score = ScoreModeNative(W, H, EdidW, EdidH, HasEdidTarget);
            }

            BootDbg("  Mode %d: %dx%d (Score: %d)\n", i, W, H, Score);

            if (Score > BestScore) {
                BestScore = Score;
                BestMode = i;
                BestW = W;
                BestH = H;
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
        BootDbg("ToyBoot: %u Unique GOP Modes For Settings", ModeCount);
        if (!InVm && HasEdidTarget) {
            BootDbg(" (List: EDID %dx%d First)\n", EdidW, EdidH);
        } else {
            BootDbg("\n");
        }
    }

    if (HasCfgTarget && !CfgMatched) {
        if (SameAspectRatio(BestW, BestH, CfgW, CfgH)) {
            BootDbg("ToyBoot: Mode %dx%d Not In GOP; Nearest Same-Aspect %dx%d\n",
                  CfgW, CfgH, BestW, BestH);
        } else {
            BootDbg("ToyBoot: Mode %dx%d Not In GOP; Nearest %dx%d\n",
                  CfgW, CfgH, BestW, BestH);
        }
    } else if (!HasCfgTarget && HasEdidTarget &&
               (BestW != EdidW || BestH != EdidH)) {
        if (SameAspectRatio(BestW, BestH, EdidW, EdidH)) {
            BootDbg("ToyBoot: EDID %dx%d Not In GOP; Nearest Same-Aspect %dx%d\n",
                  EdidW, EdidH, BestW, BestH);
        } else {
            BootDbg("ToyBoot: EDID %dx%d Not In GOP; Nearest %dx%d\n",
                  EdidW, EdidH, BestW, BestH);
        }
    }

    gBS->FreePool(HandleBuffer);

    if (BestScore == 0) {
        BootDbg("ToyBoot: No Usable GOP Mode Found\n");
        return EFI_NOT_FOUND;
    }

    sGop = Gop;
    sBestMode = BestMode;
    sBestW = BestW;
    sBestH = BestH;
    sBestScore = BestScore;
    sInVm = InVm;
    sHasCfgTarget = HasCfgTarget;
    sCfgMatched = CfgMatched;
    sHasEdidTarget = HasEdidTarget;
    sCfgW = CfgW;
    sCfgH = CfgH;
    sEdidW = EdidW;
    sEdidH = EdidH;
    sModeCount = ModeCount;

    BootSerialPrintf("Boot: GetVideoInfo Done (Pick %dx%d Mode %d)\n",
                     BestW, BestH, (UINT32)BestMode);
    return EFI_SUCCESS;
}

EFI_STATUS SetVideoMode(EFI_HANDLE ImageHandle, VIDEO_CONFIG *VideoConfig,
                        X64_BOOT_CONFIG *BootConfig) {
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = sGop;

    (void)ImageHandle;

    BootSerialPrintf("Boot: SetVideoMode\n");

    if (Gop == NULL || sBestScore == 0 || VideoConfig == NULL) {
        return EFI_NOT_READY;
    }

    /* 已是目标分辨率则勿 SetMode：QEMU+GTK 下改分辨率常会整机再复位一次 */
    {
        UINT32 CurW = 0;
        UINT32 CurH = 0;

        if (Gop->Mode != NULL && Gop->Mode->Info != NULL) {
            CurW = Gop->Mode->Info->HorizontalResolution;
            CurH = Gop->Mode->Info->VerticalResolution;
        }

        if (CurW == sBestW && CurH == sBestH) {
            BootSerialPrintf("ToyBoot: Already %dx%d, Skip SetMode\n", sBestW, sBestH);
        } else if (sInVm) {
            /*
             * Guest reboot / QEMU Reset 不会重读宿主 run.sh 的 edid；若此处 SetMode
             * 改分辨率，GTK 跳变会再复位，固件又回到 edid 旧模式 → 无限重启。
             */
            BootDbg("ToyBoot: Skip SetMode %dx%d -> %dx%d On VM (QEMU+GTK Loop)\n",
                  CurW, CurH, sBestW, sBestH);
            if (sHasCfgTarget) {
                BootDbg("ToyBoot: THEME.CFG Wants %dx%d But GOP Is %dx%d\n",
                      sCfgW, sCfgH, CurW, CurH);
                BootDbg("ToyBoot: Quit QEMU Window, Then ./run-split.sh (EDID From THEME.CFG)\n");
            }
        } else {
            Status = Gop->SetMode(Gop, sBestMode);
            if (EFI_ERROR(Status)) {
                BootSerialPrintf("ToyBoot: SetMode(%d) Failed: %r\n", sBestMode, Status);
                return Status;
            }
        }
    }

    VideoConfig->FrameBufferBase = Gop->Mode->FrameBufferBase;
    VideoConfig->FrameBufferSize = Gop->Mode->FrameBufferSize;
    VideoConfig->HorizontalResolution = Gop->Mode->Info->HorizontalResolution;
    VideoConfig->VerticalResolution = Gop->Mode->Info->VerticalResolution;
    VideoConfig->PixelsPerScanLine = Gop->Mode->Info->PixelsPerScanLine;

    if (sHasCfgTarget && sCfgMatched &&
        VideoConfig->HorizontalResolution == sCfgW &&
        VideoConfig->VerticalResolution == sCfgH) {
        BootDbg("ToyBoot: Display %dx%d (THEME.CFG)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    } else if (sHasCfgTarget &&
               (VideoConfig->HorizontalResolution != sCfgW ||
                VideoConfig->VerticalResolution != sCfgH)) {
        if (sInVm) {
            BootDbg("ToyBoot: Display %dx%d (GOP; THEME.CFG %dx%d Not Applied — Relaunch QEMU)\n",
                  VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution,
                  sCfgW, sCfgH);
        } else {
            BootDbg("ToyBoot: Display %dx%d (Nearest To THEME.CFG %dx%d)\n",
                  VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution,
                  sCfgW, sCfgH);
        }
    } else if (sInVm) {
        BootDbg("ToyBoot: Display %dx%d (QEMU/VM)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    } else if (sHasEdidTarget &&
               VideoConfig->HorizontalResolution == sEdidW &&
               VideoConfig->VerticalResolution == sEdidH) {
        BootDbg("ToyBoot: Display %dx%d (EDID Native)\n", sEdidW, sEdidH);
    } else {
        BootDbg("ToyBoot: Display %dx%d (Hardware Best Match)\n",
              VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);
    }

    BootDbg("Selected Mode: %d, Final %dx%d\n",
          sBestMode, VideoConfig->HorizontalResolution, VideoConfig->VerticalResolution);

    if (BootConfig != NULL) {
        BootConfig->VideoModeCount = sModeCount;
        BootConfig->GopProtocol = (UINT64)(UINTN)Gop;
        BootConfig->VideoModePad = TOY_BOOT_GOP_HANDOFF_MAGIC;
    }

    if (Gop->Mode != NULL && Gop->Mode->Info != NULL) {
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;
        UINTN W = Gop->Mode->Info->HorizontalResolution;
        UINTN H = Gop->Mode->Info->VerticalResolution;

        Black.Blue = 0;
        Black.Green = 0;
        Black.Red = 0;
        Black.Reserved = 0;
        (void)Gop->Blt(Gop, &Black, EfiBltVideoFill, 0, 0, 0, 0, W, H, 0);
        BootSerialPrintf("Boot: GOP Cleared %ux%u\n", (UINT32)W, (UINT32)H);
    }

    BootSerialPrintf("Boot: SetVideoMode Done\n");
    return EFI_SUCCESS;
}
