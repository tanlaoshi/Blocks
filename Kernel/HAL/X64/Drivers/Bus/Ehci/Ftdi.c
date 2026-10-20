/*
 * Ftdi.c — Claim 入口 + Bulk TX/RX tee（PR-H-ehci-4）
 *
 * RX：剥 FTDI 2 字节状态前缀 → 字符环；HalSerial 轮询。
 */
#include "Ehci/Private.h"
#include "XHCI.h"
#include "SerialLog.h"

#define FTDI_RX_Q   256

static char gRxQ[FTDI_RX_Q];
static UINT32 gRxR;
static UINT32 gRxW;
static int gTxBusy;

int FtdiReady(void) {
    return (gEhciFtdiOk && gEhciFtdiCtrl) ? 1 : 0;
}

USB_UART_OPS gFtdiEhci;

void FtdiEhciPublish(void) {
    gFtdiEhci.Claim = FtdiClaim;
    gFtdiEhci.Ready = FtdiReady;
}

/* MSC hub 扫口会 Reset 子设备；作废以免 tee 打到已死 addr */
void FtdiInvalidate(void) {
    gEhciFtdiOk = 0;
    gEhciFtdiCtrl = 0;
}

int FtdiClaim(void) {
    int i;

    if (!gEhciReady) {
        return -1;
    }
    if (FtdiReady()) {
        return 1;
    }
    BootMarkUsb("Boot: EHCI FTDI Claim Begin\n");
    for (i = 0; i < gEhciCount; i++) {
        EHCI_CTRL *C = &gEhci[i];

        if (!C->Up || !C->Sched) {
            continue;
        }
        if (C->HubAddr && FtdiClaimViaHub(C)) {
            return 1;
        }
    }
    BootMarkUsb("Boot: EHCI FTDI None\n");
    return 0;
}

/* 0=ok；<0=失败（未认/忙/Bulk）。HalSerial tee 可忽略返回值。 */
int FtdiWrite(const char *Text) {
    EHCI_CTRL *C = gEhciFtdiCtrl;
    UINT32 Mps;
    UINT32 N;
    UINT8 Buf[64];
    int Sent = 0;

    if (!Text || !FtdiReady() || gTxBusy) {
        return -1;
    }
    gTxBusy = 1;
    Mps = gEhciFtdiMpsOut ? gEhciFtdiMpsOut : 64;
    if (Mps > sizeof(Buf)) {
        Mps = (UINT32)sizeof(Buf);
    }
    while (*Text) {
        N = 0;
        while (Text[N] && N < Mps) {
            Buf[N] = (UINT8)Text[N];
            N++;
        }
        if (BulkXfer(C, gEhciFtdiAddr, gEhciFtdiEpOut, gEhciFtdiMpsOut,
                         gEhciFtdiSpeed, gEhciFtdiHubAddr, gEhciFtdiHubPort, 0,
                         Buf, N, &gEhciFtdiDtOut) < 0) {
            gTxBusy = 0;
            return -1;
        }
        Text += N;
        Sent = 1;
    }
    gTxBusy = 0;
    return Sent ? 0 : -1;
}

void FtdiPollRx(void) {
    /*
     * ehci-4 验收：CoolTerm 收 tee（TX）。
     * 同步 Bulk IN 空读会堵在 WaitQtd（NAK 重试），RX 另刀再做短超时/周期 qTD。
     */
    (void)gEhciFtdiCtrl;
}

int FtdiDataReady(void) {
    return (FtdiReady() && gRxR != gRxW) ? 1 : 0;
}

char FtdiReadChar(void) {
    char Ch;

    if (gRxR == gRxW) {
        return 0;
    }
    Ch = gRxQ[gRxR];
    gRxR = (gRxR + 1u) % FTDI_RX_Q;
    return Ch;
}
