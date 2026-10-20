/*
 * MscBot.c — BOT + capacity + 扇区（PR-H-ehci-3）
 *
 * data/csw 失败：ClearHalt + 复位 DT，再读 CSW（BOT 规范）。
 */
#include "Ehci/Private.h"
#include "UsbHostController.h"
#include "SerialLog.h"
#include "Msc/Private.h"

static int ClearHalt(EHCI_CTRL *C, UINT8 EpAddr) {
    USB_SETUP_PACKET S;

    S.bmRequestType = 0x02;
    S.bRequest = 0x01; /* CLEAR_FEATURE */
    S.wValue = 0;      /* ENDPOINT_HALT */
    S.wIndex = EpAddr;
    S.wLength = 0;
    C->XferSpeed = C->MscSpeed;
    C->XferHubAddr = C->MscHubAddr;
    C->XferHubPort = C->MscHubPort;
    return ControlTransfer(C, C->MscAddr, C->MscEpMax0 ? C->MscEpMax0 : 64, &S,
                           0) == 0;
}

static void BulkRecover(EHCI_CTRL *C, int DataIn) {
    UINT8 Ep = DataIn ? C->MscEpIn : C->MscEpOut;

    (void)ClearHalt(C, Ep);
    if (DataIn) {
        C->MscDtIn = 0;
    } else {
        C->MscDtOut = 0;
    }
    Delay(50000);
}

int MscBot(EHCI_CTRL *C, UINT8 *CbwCb, UINT8 CbLen, UINT32 DataLen,
               int DataIn, void *Data) {
    UINT8 Cbw[32];
    UINT8 Csw[16];
    int DataRc = 0;

    if (!C || !C->MscOk) {
        return -1;
    }
    if (CbLen == 0 || CbLen > 16) {
        return -1;
    }
    if (DataLen != 0 && Data == 0) {
        return -1;
    }
    if (DataLen > 512) {
        return -1;
    }

    gEhciMscTag++;
    CommandFill(Cbw, gEhciMscTag, CbwCb, CbLen, DataLen, DataIn);

    C->XferSpeed = C->MscSpeed;
    C->XferHubAddr = C->MscHubAddr;
    C->XferHubPort = C->MscHubPort;

    if (BulkOn(USB_HOST_CONTROLLER_EHCI, 0, Cbw, 31, 1) < 0) {
        BootMarkUsb("Boot: EHCI MSC cbw fail ");
        BootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
        BootMarkUsb("\n");
        BulkRecover(C, 0);
        return -1;
    }
    if (DataLen != 0) {
        DataRc = BulkOn(USB_HOST_CONTROLLER_EHCI, 0, Data, DataLen, DataIn ? 0 : 1);
        if (DataRc < 0) {
            BootMarkUsb("Boot: EHCI MSC data fail ");
            BootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
            BootMarkUsb("\n");
            BulkRecover(C, DataIn);
        }
    }
    MscZero(Csw, sizeof(Csw));
    if (BulkOn(USB_HOST_CONTROLLER_EHCI, 0, Csw, 13, 0) < 0) {
        BootMarkUsb("Boot: EHCI MSC csw fail ");
        BootMarkUsb(gEhciLastErr ? gEhciLastErr : "?");
        BootMarkUsb("\n");
        BulkRecover(C, 1);
        return -1;
    }
    if (DataRc < 0) {
        return -1;
    }
    if (!StatusSignature(Csw)) {
        BootMarkUsb("Boot: EHCI MSC csw sig\n");
        return -1;
    }
    if (Csw[12] != 0) {
        if (!gEhciMscInSense) {
            UINT8 Sense[18];
            UINT8 Scdb[16];

            MscMarkHex4("Boot: EHCI MSC status=", Csw[12]);
            BootMarkUsb("\n");
            gEhciMscInSense = 1;
            MscZero(Scdb, sizeof(Scdb));
            Scdb[0] = 0x03;
            Scdb[4] = 18;
            MscZero(Sense, sizeof(Sense));
            (void)MscBot(C, Scdb, 6, 18, 1, Sense);
            gEhciMscInSense = 0;
        }
        return -1;
    }
    return 0;
}

int MscCapacity(void) {
    EHCI_CTRL *C = gEhciMscCtrl;
    UINT8 Inquiry[36];
    UINT8 Cap[8];
    UINT8 Cdb[16];
    UINT32 LastLba;
    UINT32 Bsz;

    if (!C || !C->MscOk) {
        return -1;
    }
    C->MscCapacityOk = 0;
    C->MscBlockCount = 0;
    C->MscBlockSize = 0;

    MscZero(Cdb, sizeof(Cdb));
    Cdb[0] = 0x12;
    Cdb[4] = 36;
    MscZero(Inquiry, sizeof(Inquiry));
    if (MscBot(C, Cdb, 6, 36, 1, Inquiry) < 0) {
        BootMarkUsb("Boot: EHCI MSC inquiry fail\n");
        return -1;
    }

    /* 空读卡器 / 未起转：TUR 清 sense，再 readcap */
    MscZero(Cdb, sizeof(Cdb));
    Cdb[0] = 0x00; /* TEST UNIT READY */
    (void)MscBot(C, Cdb, 6, 0, 0, 0);
    Delay(100000);

    MscZero(Cdb, sizeof(Cdb));
    Cdb[0] = 0x25;
    MscZero(Cap, sizeof(Cap));
    if (MscBot(C, Cdb, 10, 8, 1, Cap) < 0) {
        BootMarkUsb("Boot: EHCI MSC readcap fail\n");
        return -1;
    }
    LastLba = ((UINT32)Cap[0] << 24) | ((UINT32)Cap[1] << 16) |
              ((UINT32)Cap[2] << 8) | (UINT32)Cap[3];
    Bsz = ((UINT32)Cap[4] << 24) | ((UINT32)Cap[5] << 16) |
          ((UINT32)Cap[6] << 8) | (UINT32)Cap[7];
    if (Bsz == 0) {
        return -1;
    }
    C->MscBlockCount = LastLba + 1u;
    C->MscBlockSize = Bsz;
    C->MscCapacityOk = 1;
    MscMarkHex4("Boot: EHCI MSC blocks=", C->MscBlockCount);
    MscMarkHex4(" bsize=", C->MscBlockSize);
    BootMarkUsb("\n");
    return 0;
}

static int XferSectors(UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    EHCI_CTRL *C = gEhciMscCtrl;
    UINT8 Bounce[512];
    UINT8 *Ptr = (UINT8 *)Buffer;
    UINT32 i;

    if (!C || !C->MscCapacityOk || C->MscBlockSize != 512 || !Buffer ||
        Count == 0) {
        return 0;
    }
    if (Lba >= C->MscBlockCount || Count > C->MscBlockCount - Lba) {
        return 0;
    }
    for (i = 0; i < Count; i++) {
        UINT8 Cdb[16];
        UINT32 Cur = Lba + i;

        MscZero(Cdb, sizeof(Cdb));
        Cdb[0] = Write ? 0x2Au : 0x28u;
        Cdb[2] = (UINT8)(Cur >> 24);
        Cdb[3] = (UINT8)(Cur >> 16);
        Cdb[4] = (UINT8)(Cur >> 8);
        Cdb[5] = (UINT8)(Cur);
        Cdb[7] = 0;
        Cdb[8] = 1;
        if (Write) {
            MscCopy(Bounce, Ptr + (UINTN)i * 512u, 512);
            if (MscBot(C, Cdb, 10, 512, 0, Bounce) < 0) {
                return 0;
            }
        } else {
            MscZero(Bounce, sizeof(Bounce));
            if (MscBot(C, Cdb, 10, 512, 1, Bounce) < 0) {
                return 0;
            }
            MscCopy(Ptr + (UINTN)i * 512u, Bounce, 512);
        }
    }
    return 1;
}

int MscReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return XferSectors(Lba, Count, Buffer, 0);
}

int MscWriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    return XferSectors(Lba, Count, (void *)Buffer, 1);
}

int MscFlush(void) {
    return 1;
}
