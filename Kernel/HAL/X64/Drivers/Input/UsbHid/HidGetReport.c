/*
 * HidGetReport.c — PR-S-xhcihid-1：GET_REPORT / 键盘 EP0 兜底
 * 从 Hid.c 原样搬家；不改语义。HID 全局仍在 Xhci.c。
 */
#include "UsbHid/Private.h"

/* HID GET_REPORT(Input)：复合设备键盘中断 IN 不完成时的 EP0 兜底 */
int HidGetInputReport(UINT8 Interface, void *Data, UINT16 Length) {
    USB_SETUP Setup = {
        .bmRequestType = 0xA1,
        .bRequest = 0x01,
        .wValue = USB_HID_REPORT_IN,
        .wIndex = Interface,
        .wLength = Length
    };
    if (gSlotId == 0) {
        return -1;
    }
    gTransferSlot = gSlotId;
    return ControlTransfer(&Setup, Data);
}

/*
 * 真机复合：PHOTO 上键 EP(DCI=5) 从不完成、鼠 DCI=3 正常。
 * 在 Drain 释锁后轮询 GET_REPORT；与中断鼠并行，勿持 gHidQueueLock。
 */
void PollKeyboardGetReport(void) {
    UINT8 Buf[8];
    int i;
    int Diff;

    if (!gKeyboardPollReport || gSlotId == 0) {
        return;
    }
    if (gGetReportBusy) {
        return;
    }
    gGetReportBusy = 1;
    ZeroMemory(Buf, sizeof(Buf));
    gTransferFast = 1;
    if (HidGetInputReport(gKeyboardInterface, Buf, 8) < 0) {
        gTransferFast = 0;
        gGetReportFails++;
        if (gGetReportFails == 1) {
            BootLog("Boot: XHCI get-report stall/retry\n");
        }
        if (gGetReportFails >= 32) {
            gKeyboardPollReport = 0;
            BootLog("Boot: XHCI kbd get-report give up\n");
        }
        gGetReportBusy = 0;
        return;
    }
    gTransferFast = 0;
    gGetReportFails = 0;
    Diff = 0;
    for (i = 0; i < 8; i++) {
        if (Buf[i] != gKeyboardReportPrevious[i]) {
            Diff = 1;
            break;
        }
    }
    if (Diff) {
        for (i = 0; i < 8; i++) {
            gKeyboardReportPrevious[i] = Buf[i];
            gReportBuf[i] = Buf[i];
        }
        FlushDma(gReportBuf, sizeof(gReportBuf));
        KeyboardPush();
        gStatKeyboardPush++;
        gStatIntrEvt++;
    }
    gGetReportBusy = 0;
}

/*
 * 曾有 ReAddKeyboardInterruptOnly / RecoverKeyboardInterrupt / EnableKeyboardGetReport：
 * GET_REPORT 易 Stall；recover 未再挂入枚举路径 → 已删，消 unused 警告。
 * HID GET_REPORT 曾作 poll 兜底；持 gHidQueueLock 时调用会死锁，故已从 Drain 移除。
 */
