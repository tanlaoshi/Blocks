/*
 * Summary.c — 设备管理器「系统摘要」只读数据采集（PR-DEV-ui-summary-data）
 *
 * 第 1 刀：只建数据模型 + 填充；不改 Paint 布局。
 * 数据源均走公共 API：Hal* / PhysicalMemory* / Block* / BootHandoff / Device。
 * 不 include HAL 私有头。十进制格式化自备，不拉巨型 printf。
 */
#include "DevicesUiPrivate.h"
#include "OsVersion.h"
#include "Hal.h"
#include "PhysicalMemory.h"
#include "Block.h"
#include "BootHandoff.h"
#include "Debug.h"

/* ---- 极简缓冲写入（自带 NUL 终止，越界截断） ---- */

typedef struct {
    char  *P;
    UINTN  N;
    UINTN  Max;
} BUF;

static void BufferInitialize(BUF *B, char *P, UINTN Max) {
    B->P = P;
    B->N = 0;
    B->Max = Max;
    if (Max > 0) {
        P[0] = '\0';
    }
}

static void BufferFinalize(BUF *B) {
    UINTN I = B->N < B->Max ? B->N : B->Max - 1;
    B->P[I] = '\0';
}

static void BufferCharacter(BUF *B, char C) {
    if (B->N + 1 < B->Max) {
        B->P[(B->N)++] = C;
    }
}

static void BufferString(BUF *B, const char *S) {
    if (!S) {
        return;
    }
    while (*S) {
        BufferCharacter(B, *S++);
    }
}

static void BufferDecimal64(BUF *B, UINT64 V) {
    char Tmp[24];
    int  I = 0;

    if (V == 0) {
        BufferCharacter(B, '0');
        return;
    }
    while (V > 0 && I < (int)sizeof(Tmp)) {
        Tmp[I++] = (char)('0' + (V % 10u));
        V /= 10u;
    }
    while (I > 0) {
        BufferCharacter(B, Tmp[--I]);
    }
}

/* ---- 填充 ---- */

void DevicesUiFillSummary(DEVICES_UI_SUMMARY *Out) {
    BUF         Disk;
    BUF         Disp;
    const BOOT_HANDOFF *Bi;
    UINT64      Pages;
    int         Ready;
    UINT32      D;

    if (!Out) {
        return;
    }

    Out->OsVersion  = OS_VERSION_STRING;
    Out->Arch      = HalArchName();
    Out->CpuInfo   = HalCpuInfo();
    Out->CpuCount  = HalCpuCount();
    Out->Hypervisor = HalCpuIsHypervisor();
    Out->PciCount  = DeviceCount();

    /* 内存：4KB 页 → MiB（1 MiB = 256 页） */
    Pages = PhysicalMemoryTotalPages();
    Out->MemTotalMiB = Pages / 256u;
    Pages = PhysicalMemoryFreePageCount();
    Out->MemFreeMiB = Pages / 256u;

    /* 硬盘：首版只报就绪盘数；不做精确容量 / SMART */
    Ready = 0;
    for (D = 0; D < BLOCK_MAX_DRIVES; D++) {
        if (BlockDriveReady(D)) {
            Ready++;
        }
    }
    BufferInitialize(&Disk, Out->Disk, sizeof(Out->Disk));
    if (Ready > 0) {
        BufferDecimal64(&Disk, (UINT64)Ready);
        BufferCharacter(&Disk, ' ');
        BufferString(&Disk, LocaleString(MESSAGE_DEV_READY));
    } else {
        BufferCharacter(&Disk, '-');
    }
    BufferFinalize(&Disk);

    /* 显示器：BootHandoff 帧缓冲宽高；无 FB 则「-」 */
    BufferInitialize(&Disp, Out->Display, sizeof(Out->Display));
    Bi = BootHandoffGet();
    if (HalHasFrameBuffer() && Bi &&
        Bi->HorizontalResolution && Bi->VerticalResolution) {
        BufferDecimal64(&Disp, (UINT64)Bi->HorizontalResolution);
        BufferCharacter(&Disp, 'x');
        BufferDecimal64(&Disp, (UINT64)Bi->VerticalResolution);
    } else {
        BufferCharacter(&Disp, '-');
    }
    BufferFinalize(&Disp);
}

/* ---- DEBUG 自检：填结构体并串口打一行 ---- */

void DevicesUiSummarySelfCheck(void) {
    DEVICES_UI_SUMMARY S;
    BUF  L;
    char Line[192];

    DevicesUiFillSummary(&S);
    BufferInitialize(&L, Line, sizeof(Line));
    BufferString(&L, "summary: os=");
    BufferString(&L, S.OsVersion ? S.OsVersion : "-");
    BufferString(&L, " arch=");
    BufferString(&L, S.Arch ? S.Arch : "-");
    BufferString(&L, " cpu=");
    BufferString(&L, S.CpuInfo ? S.CpuInfo : "-");
    BufferString(&L, " x");
    BufferDecimal64(&L, (UINT64)S.CpuCount);
    BufferString(&L, " mem=");
    BufferDecimal64(&L, S.MemFreeMiB);
    BufferCharacter(&L, '/');
    BufferDecimal64(&L, S.MemTotalMiB);
    BufferString(&L, "MiB disk=");
    BufferString(&L, S.Disk);
    BufferString(&L, " disp=");
    BufferString(&L, S.Display);
    BufferString(&L, " pci=");
    BufferDecimal64(&L, (UINT64)S.PciCount);
    BufferCharacter(&L, '\n');
    BufferFinalize(&L);
    DebugWrite(Line);
}

/* ---- About 页绘制（PR-DEV-ui-summary-paint） ---- */

static void SumDrawRow(UINT32 LblX, UINT32 ValX, UINT32 Right, UINT32 *Ty, UINT32 MaxY,
                       UINT32 LineH, const char *Lbl, const char *Val) {
    UINT32 Ny;
    UINT32 MaxW;

    if (*Ty + LineH > MaxY) {
        return;
    }
    if (Lbl) {
        HalVideoDrawStringAt(LblX, *Ty, Lbl, ThemeTextMuted());
    }
    if (Val) {
        MaxW = Right > ValX ? Right - ValX : 0;
        if (MaxW < 8u) {
            MaxW = 8u;
        }
        Ny = UiDrawTextWrap(ValX, *Ty, MaxW, MaxY, LineH + 2u, Val, ThemeText());
        if (Ny > *Ty) {
            *Ty = Ny;
            return;
        }
    }
    *Ty += LineH + 2;
}

static void SumComposeOs(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;

    BufferInitialize(&B, Out, Max);
    BufferString(&B, S->OsVersion ? S->OsVersion : "-");
    if (S->Arch && S->Arch[0]) {
        BufferCharacter(&B, ' ');
        BufferCharacter(&B, '(');
        BufferString(&B, S->Arch);
        BufferCharacter(&B, ')');
    }
    BufferFinalize(&B);
}
static void SumComposeCpu(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    BufferInitialize(&B, Out, Max);
    BufferString(&B, S->CpuInfo ? S->CpuInfo : "-");
    BufferString(&B, " x");
    BufferDecimal64(&B, (UINT64)(S->CpuCount > 0 ? S->CpuCount : 1));
    BufferFinalize(&B);
}

static void BufferMemoryNumber(BUF *B, UINT64 MiB, int AsGib) {
    if (!AsGib) {
        BufferDecimal64(B, MiB);
        return;
    }
    BufferDecimal64(B, MiB / 1024u);
    BufferCharacter(B, '.');
    BufferCharacter(B, (char)('0' + (MiB % 1024u) * 10u / 1024u));
}

static void SumComposeMem(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    int AsGib = S->MemTotalMiB > 1024u;
    BufferInitialize(&B, Out, Max);
    BufferMemoryNumber(&B, S->MemFreeMiB, AsGib);
    BufferCharacter(&B, '/');
    BufferMemoryNumber(&B, S->MemTotalMiB, AsGib);
    BufferCharacter(&B, ' ');
    BufferString(&B, AsGib ? "GiB" : "MiB");
    BufferFinalize(&B);
}

static void SumComposeHyper(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;
    BufferInitialize(&B, Out, Max);
    BufferString(&B, S->Hypervisor ? LocaleString(MESSAGE_DEV_YES) : LocaleString(MESSAGE_DEV_NO));
    BufferFinalize(&B);
}

static void SumComposePci(char *Out, UINTN Max, const DEVICES_UI_SUMMARY *S) {
    BUF B;

    BufferInitialize(&B, Out, Max);
    BufferDecimal64(&B, (UINT64)S->PciCount);
    BufferFinalize(&B);
}

static UINT32 SumMax(UINT32 A, UINT32 B) {
    return A > B ? A : B;
}

void DevicesUiPaintSummary(UINT32 X, UINT32 Y, UINT32 W, UINT32 H) {
    DEVICES_UI_SUMMARY S;
    UINT32 Ty;
    UINT32 MaxY;
    UINT32 LblX;
    UINT32 ValX;
    UINT32 LineH;
    UINT32 MaxLblW;
    char Val[96];

    if (W < 40 || H < 60) {
        return;
    }
    DevicesUiFillSummary(&S);

    GraphicsFillRectangle(X, Y, W, H, ThemePanelDetailBackground());
    if (W > 3) {
        GraphicsFillRectangle(X, Y, 3, H, ThemePanelSeparator());
    }

    LineH = FontAdvanceY();
    if (LineH < 16) {
        LineH = 16;
    }
    LblX = X + 14;
    /* 测量最长标签，值列起点跟着走，避免标签/值重叠 */
    MaxLblW = 0;
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_OS)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_CPU)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_MEMORY)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_DISKS)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_DISPLAY)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_HYPERVISOR)));
    MaxLblW = SumMax(MaxLblW, FontStringWidth(LocaleString(MESSAGE_DEV_PCI_COUNT)));
    ValX = LblX + MaxLblW + 12;
    if (ValX > X + W - 60) {
        ValX = X + W - 60;
    }
    Ty = Y + 12;
    MaxY = Y + H - 4;

    /* 标题 */
    if (Ty + LineH <= MaxY) {
        HalVideoDrawStringAt(LblX, Ty, LocaleString(MESSAGE_DEV_ABOUT), ThemeTextAccent());
        Ty += LineH + 6;
    }

    SumComposeOs(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_OS), Val);
    SumComposeCpu(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_CPU), Val);
    SumComposeMem(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_MEMORY), Val);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_DISKS), S.Disk);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_DISPLAY),
               S.Display);
    SumComposeHyper(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_HYPERVISOR), Val);
    SumComposePci(Val, sizeof(Val), &S);
    SumDrawRow(LblX, ValX, X + W - 8u, &Ty, MaxY, LineH, LocaleString(MESSAGE_DEV_PCI_COUNT), Val);
}
