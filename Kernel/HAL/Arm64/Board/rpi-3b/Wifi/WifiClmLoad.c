/*
 * WifiClmLoad.c — 固件起来后灌 CLM（Country Locale Matrix）
 * 对表 cyw43_clm_load / Linux brcmf clm_blob；缺 CLM 时 country 拒、接口难 up。
 */
#include "Wifi/Private.h"

#define WIFI_CLM_PATH_43430 \
    "Assets/Firmware/brcm/brcmfmac43430-sdio.clm_blob"
#define WIFI_CLM_MAXIMUM_BYTES 8192u
/* cyw43 SDIO：1024+512；本包 ≤ SDPCM 缓冲 */
#define WIFI_CLM_CHUNK_BYTES   1536u
#define WIFI_CLM_DOWNLOAD_HANDLER_VERSION (1u << 12)
#define WIFI_CLM_DOWNLOAD_BEGIN           2u
#define WIFI_CLM_DOWNLOAD_END             4u

static void WifiClmMemoryCopy(UINT8 *Destination, const UINT8 *Source, UINT32 Bytes) {
    UINT32 Index;
    for (Index = 0; Index < Bytes; Index++) {
        Destination[Index] = Source[Index];
    }
}

static void WifiClmWriteLittle16(UINT8 *Destination, UINT16 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
}

static void WifiClmWriteLittle32(UINT8 *Destination, UINT32 Value) {
    Destination[0] = (UINT8)(Value & 0xFFu);
    Destination[1] = (UINT8)((Value >> 8) & 0xFFu);
    Destination[2] = (UINT8)((Value >> 16) & 0xFFu);
    Destination[3] = (UINT8)((Value >> 24) & 0xFFu);
}

static int WifiClmReadBlob(UINT8 **OutBlob, UINTN *OutBytes) {
    UINT8 Probe[16];
    UINTN Size = 0;
    UINTN Got = 0;
    UINT32 Pages;
    void *Memory;

    *OutBlob = 0;
    *OutBytes = 0;
    if (HalDriverFileRead(WIFI_CLM_PATH_43430, Probe, sizeof(Probe), &Size) != 0 ||
        Size == 0) {
        return -1;
    }
    Pages = (WIFI_CLM_MAXIMUM_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE;
    Memory = PhysicalMemoryAllocatePages(Pages);
    if (Memory == 0) {
        return -1;
    }
    if (HalDriverFileRead(WIFI_CLM_PATH_43430, Memory, Pages * PAGE_SIZE, &Got) != 0 ||
        Got < 64u || Got > WIFI_CLM_MAXIMUM_BYTES) {
        PhysicalMemoryFreePages(Memory, Pages);
        return -1;
    }
    *OutBlob = (UINT8 *)Memory;
    *OutBytes = Got;
    return 0;
}

/*
 * iovar：name[8]="clmload\0" + flag:u16 + type:u16=2 + len:u32 + pad:u32 + data
 * 对表 cyw43_clm_load；整段长度 8 字节对齐。
 */
static int WifiClmSendChunk(const UINT8 *Data, UINT32 Length, UINT16 Flag) {
    UINT8 Buffer[20u + WIFI_CLM_CHUNK_BYTES + 8u];
    UINT32 PayloadBytes;
    UINT32 Index;

    if (Length > WIFI_CLM_CHUNK_BYTES) {
        return -1;
    }
    for (Index = 0; Index < sizeof(Buffer); Index++) {
        Buffer[Index] = 0;
    }
    WifiClmMemoryCopy(Buffer, (const UINT8 *)"clmload", 8u);
    WifiClmWriteLittle16(Buffer + 8, Flag);
    WifiClmWriteLittle16(Buffer + 10, 2u);
    WifiClmWriteLittle32(Buffer + 12, Length);
    WifiClmWriteLittle32(Buffer + 16, 0);
    WifiClmMemoryCopy(Buffer + 20, Data, Length);
    PayloadBytes = 20u + Length;
    PayloadBytes = (PayloadBytes + 7u) & ~7u;
    return WifiIoctl(WIFI_SDPCM_SET, WIFI_WLC_SET_VAR, Buffer, PayloadBytes, 0);
}

static int WifiClmCheckStatus(void) {
    UINT8 Buffer[19];
    UINT32 OutBytes = 0;
    UINT32 Status;
    UINT32 Index;

    for (Index = 0; Index < sizeof(Buffer); Index++) {
        Buffer[Index] = 0;
    }
    WifiClmMemoryCopy(Buffer, (const UINT8 *)"clmload_status", 15u);
    if (WifiIoctl(WIFI_SDPCM_GET, WIFI_WLC_GET_VAR, Buffer, sizeof(Buffer), &OutBytes) != 0) {
        return -1;
    }
    if (OutBytes < 4u) {
        return -1;
    }
    Status = (UINT32)Buffer[0] | ((UINT32)Buffer[1] << 8) | ((UINT32)Buffer[2] << 16) |
             ((UINT32)Buffer[3] << 24);
    return (Status == 0u) ? 0 : -1;
}

int WifiClmLoad(void) {
    UINT8 *Blob = 0;
    UINTN Bytes = 0;
    UINTN Offset = 0;
    int Result = -1;

    if (gWifiChipIdentity != WIFI_CHIP_ID_43430) {
        /* 本板路径只钉 43430；其它芯片暂跳过 */
        return 0;
    }
    if (WifiClmReadBlob(&Blob, &Bytes) != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI clm miss\n");
        return -1;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI clm load begin\n");
    while (Offset < Bytes) {
        UINT32 Chunk = WIFI_CLM_CHUNK_BYTES;
        UINT16 Flag = (UINT16)WIFI_CLM_DOWNLOAD_HANDLER_VERSION;

        if (Offset == 0u) {
            Flag = (UINT16)(Flag | WIFI_CLM_DOWNLOAD_BEGIN);
        }
        if (Offset + Chunk >= Bytes) {
            Chunk = (UINT32)(Bytes - Offset);
            Flag = (UINT16)(Flag | WIFI_CLM_DOWNLOAD_END);
        }
        if (WifiClmSendChunk(Blob + Offset, Chunk, Flag) != 0) {
            HalSerialBootMarkForce("Boot: Pi WIFI clm chunk fail\n");
            goto Done;
        }
        Offset += Chunk;
    }
    if (WifiClmCheckStatus() != 0) {
        HalSerialBootMarkForce("Boot: Pi WIFI clm status fail\n");
        goto Done;
    }
    HalSerialBootMarkForce("Boot: Pi WIFI clm ok\n");
    Result = 0;

Done:
    if (Blob != 0) {
        PhysicalMemoryFreePages(
            Blob, (WIFI_CLM_MAXIMUM_BYTES + PAGE_SIZE - 1u) / PAGE_SIZE);
    }
    return Result;
}
