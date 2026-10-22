/*
 * MscBot.c — PR-S-xhcimsc-1：从 Msc.c 原样搬家；不改语义。
 * MSC 全局仍定义在 Xhci.c（BSS 顺序影响 HID DMA 环；勿迁出）。
 */
#include "Compiler.h"
#include "Atomic.h"
#include "XHCI/Private.h"
#include "UsbHostController.h"
#include "Msc/Private.h"

static int gMscInSense;  /* REQUEST SENSE 重入保护 */

/*
 * BOT：CBW → [DATA] → CSW。DataIn=1 时数据走 Bulk IN。
 * 成功 0；失败 -1。CSW≠0 时发 REQUEST SENSE 清粘滞 sense（不少棒必须）。
 */
static int Bot(UINT8 *CbwCb, UINT8 CbLen, UINT32 DataLen, int DataIn,
                  void *Data) {
    UINT8 Cbw[32] BLOCKS_ALIGNED(64);
    UINT8 Csw[16] BLOCKS_ALIGNED(64);
    static UINT32 Tag;
    int Locked = 0;
    int Rc = -1;

    if (!gMscClaimed) {
        return -1;
    }
    if (CbLen == 0 || CbLen > 16) {
        return -1;
    }
    if (DataLen != 0 && Data == 0) {
        return -1;
    }

    /*
     * 整段 CBW→DATA→CSW 互斥。Desktop HotPoll 的 HubGetPortStatus 与
     * 多核 FAT 读若并行会 STALL/Babble（cc=6/3）+ CSW 签名错。
     * REQUEST SENSE 重入（gMscInSense）不再抢锁。
     * 勿用 SpinLock：持锁跨 WaitBulk 可达数百 ms（会长期 cli）。
     */
    if (!gMscInSense) {
        while (BLOCKS_ATOMIC_TAS(&gMscBotBusy, 1u)) {
            HalCpuRelax();
        }
        Locked = 1;
    }

    Tag++;
    CommandFill(Cbw, Tag, CbwCb, CbLen, DataLen, DataIn);

    if (BulkOn(USB_HOST_CONTROLLER_XHCI, 0, Cbw, 31, 1) < 0) {
        static UINT32 sCbwFailLogged;

        if (sCbwFailLogged < 2) {
            BootLog("Boot: MSC bot cbw fail\n");
            sCbwFailLogged++;
        }
        goto Out;
    }
    if (DataLen != 0) {
        if (BulkOn(USB_HOST_CONTROLLER_XHCI, 0, Data, DataLen, DataIn ? 0 : 1) < 0) {
            BootLog("Boot: MSC bot data fail\n");
            goto Out;
        }
    }
    ZeroMemory(Csw, sizeof(Csw));
    if (BulkOn(USB_HOST_CONTROLLER_XHCI, 0, Csw, 13, 0) < 0) {
        BootLog("Boot: MSC bot csw fail\n");
        goto Out;
    }
    /* USBS */
    if (!StatusSignature(Csw)) {
        BootLog("Boot: MSC bot csw sig\n");
        goto Out;
    }
    if (Csw[12] != 0) {
        if (!gMscInSense) {
            UINT8 Sense[18] BLOCKS_ALIGNED(64);
            UINT8 Scdb[16];

            BootLogHex("Boot: MSC bot status=", Csw[12], 2);
            gMscInSense = 1;
            ZeroMemory(Scdb, sizeof(Scdb));
            Scdb[0] = 0x03; /* REQUEST SENSE */
            Scdb[4] = 18;
            ZeroMemory(Sense, sizeof(Sense));
            if (Bot(Scdb, 6, 18, 1, Sense) == 0) {
                BootLogHex("Boot: MSC sense key=", Sense[2] & 0x0Fu, 2);
                BootLogHex("Boot: MSC sense asc=", Sense[12], 2);
                BootLogHex("Boot: MSC sense ascq=", Sense[13], 2);
            }
            gMscInSense = 0;
        }
        goto Out;
    }
    Rc = 0;
Out:
    if (Locked) {
        BLOCKS_ATOMIC_RELEASE(&gMscBotBusy);
    }
    return Rc;
}

/*
 * PR-H-msc-5：INQUIRY + READ CAPACITY(10)。不读分区、不挂 FAT。
 * 成功 0 并填 gMscBlockCount/Size；失败 -1。
 */
int Capacity(void) {
    UINT8 Inquiry[36] BLOCKS_ALIGNED(64);
    UINT8 Cap[8] BLOCKS_ALIGNED(64);
    UINT8 Cdb[16];
    UINT32 LastLba;
    UINT32 Bsz;

    if (!gMscClaimed || gMscScanSlot == 0) {
        BootLog("Boot: MSC capacity not claimed\n");
        return -1;
    }

    gMscCapacityOk = 0;
    gMscBlockCount = 0;
    gMscBlockSize = 0;

    ZeroMemory(Cdb, sizeof(Cdb));
    Cdb[0] = 0x12; /* INQUIRY */
    Cdb[4] = 36;
    ZeroMemory(Inquiry, sizeof(Inquiry));
    if (Bot(Cdb, 6, 36, 1, Inquiry) < 0) {
        BootLog("Boot: MSC inquiry fail\n");
        return -1;
    }
    BootLogHex("Boot: MSC inquiry pdt=", Inquiry[0] & 0x1Fu, 2);
    {
        char Line[48];
        int n = 0;
        const char *P = "Boot: MSC vendor=";
        int i;

        while (*P && n < 20) {
            Line[n++] = *P++;
        }
        for (i = 8; i < 16 && n < 46; i++) {
            char C = (char)Inquiry[i];

            if (C < 32 || C > 126) {
                C = '.';
            }
            Line[n++] = C;
        }
        Line[n++] = '\n';
        Line[n] = 0;
        BootLog(Line);
    }

    /*
     * PR-BOOT-fast-2：介质偶发未就绪（QEMU usb-storage / 慢棒）。
     * 空口 Force 已收紧；此处对 ReadCap 短重试，避免误判 Mux Fail。
     */
    {
        int Try;
        int CapOk = 0;

        for (Try = 0; Try < 3; Try++) {
            if (Try > 0) {
                StallMs(HalCpuIsHypervisor() ? 20u : 40u);
            }
            ZeroMemory(Cdb, sizeof(Cdb));
            Cdb[0] = 0x25; /* READ CAPACITY(10) */
            ZeroMemory(Cap, sizeof(Cap));
            if (Bot(Cdb, 10, 8, 1, Cap) >= 0) {
                CapOk = 1;
                break;
            }
        }
        if (!CapOk) {
            BootLog("Boot: MSC readcap fail\n");
            return -1;
        }
    }
    LastLba = ((UINT32)Cap[0] << 24) | ((UINT32)Cap[1] << 16) |
              ((UINT32)Cap[2] << 8) | (UINT32)Cap[3];
    Bsz = ((UINT32)Cap[4] << 24) | ((UINT32)Cap[5] << 16) |
          ((UINT32)Cap[6] << 8) | (UINT32)Cap[7];
    if (Bsz == 0) {
        BootLog("Boot: MSC readcap bsz0\n");
        return -1;
    }
    gMscBlockCount = LastLba + 1u;
    gMscBlockSize = Bsz;
    gMscCapacityOk = 1;
    BootLogHex("Boot: MSC blocks=", gMscBlockCount, 8);
    BootLogHex("Boot: MSC bsize=", gMscBlockSize, 8);
    return 0;
}

/*
 * PR-H-msc-6：逐扇区 BOT READ(10)/WRITE(10)。
 * 仅支持逻辑块 512；bounce 对齐。勿带 FUA/SYNC：不少棒 CSW=1 且会搞丢写缓存。
 */
static int TransferSectors(UINT32 Lba, UINT32 Count, void *Buffer, int Write) {
    UINT8 Bounce[512] BLOCKS_ALIGNED(64);
    UINT8 *Ptr = (UINT8 *)Buffer;
    UINT32 i;

    if (!gMscCapacityOk || gMscBlockSize != 512 || Buffer == 0 || Count == 0) {
        return 0;
    }
    if (Lba >= gMscBlockCount || Count > gMscBlockCount - Lba) {
        return 0;
    }

    for (i = 0; i < Count; i++) {
        UINT8 Cdb[16];
        UINT32 Cur = Lba + i;

        ZeroMemory(Cdb, sizeof(Cdb));
        Cdb[0] = Write ? 0x2Au : 0x28u; /* WRITE(10) / READ(10) */
        Cdb[2] = (UINT8)(Cur >> 24);
        Cdb[3] = (UINT8)(Cur >> 16);
        Cdb[4] = (UINT8)(Cur >> 8);
        Cdb[5] = (UINT8)(Cur);
        Cdb[7] = 0;
        Cdb[8] = 1;
        if (Write) {
            CopyMemory(Bounce, Ptr + (UINTN)i * 512u, 512);
            if (Bot(Cdb, 10, 512, 0, Bounce) < 0) {
                return 0;
            }
        } else {
            ZeroMemory(Bounce, sizeof(Bounce));
            if (Bot(Cdb, 10, 512, 1, Bounce) < 0) {
                return 0;
            }
            CopyMemory(Ptr + (UINTN)i * 512u, Bounce, 512);
        }
    }
    return 1;
}

int ReadSectors(UINT32 Lba, UINT32 Count, void *Buffer) {
    return TransferSectors(Lba, Count, Buffer, 0);
}

int WriteSectors(UINT32 Lba, UINT32 Count, const void *Buffer) {
    return TransferSectors(Lba, Count, (void *)Buffer, 1);
}

/*
 * USB MSC：多数棒不支持 SYNCHRONIZE CACHE；失败 CSW 后部分控制器会丢未刷写缓存，
 * 表现为目录项「写成功」但读回旧内容 → store remove file remains。
 * Flush 改为空操作；依赖 WRITE(10) 本身落盘。
 */
int Synchronize(void) {
    return 1;
}
