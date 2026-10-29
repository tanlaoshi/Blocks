#include "Kernel.h"
#include "Wifi/Private.h"

/*
 * SDIO 数据阶段：把字节流推进 SDHCI 缓冲，并等卡收尾。
 * 对表 Linux mmc/core 的 PIO 路径——主机只是喂 FIFO，真正的慢在卡侧背板写，
 * 所以这里的等待全部用墙钟，且失败时把「停在第几个字」打出来。
 */
int WifiSdioProgrammedIoWriteBytes(const UINT8 *Buffer, UINT32 ByteCount) {
    UINT32 Words = (ByteCount + 3u) / 4u;
    UINT32 Index;

    for (Index = 0; Index < Words; Index++) {
        UINT32 Base = Index * 4u;
        UINT32 Word = 0;
        UINT32 Elapsed = 0;

        /* 墙钟等缓冲空位：卡在慢时钟域时 FIFO 排空可能远超自旋窗口 */
        while ((SdhciRead32(SDHCI_PRESENT_STATE) & SDHCI_SPACE_AVAILABLE) == 0) {
            if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0 || Elapsed >= 50000u) {
                /* 卡不收数据：打出停在第几个字，区分「一开始就堵」和「写到一半堵」 */
                WifiBootMarkHexadecimal32("Boot: Pi WIFI fifo stall word=", Index);
                WifiBootMarkHexadecimal32("/", Words);
                HalSerialBootMarkForce("\n");
                WifiSdioProgrammedIoFailCleanup();
                return -1;
            }
            SdhciDelayMicroseconds(100);
            Elapsed++;
        }
        if (Base < ByteCount) {
            Word |= (UINT32)Buffer[Base];
        }
        if (Base + 1u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 1u] << 8;
        }
        if (Base + 2u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 2u] << 16;
        }
        if (Base + 3u < ByteCount) {
            Word |= (UINT32)Buffer[Base + 3u] << 24;
        }
        SdhciWrite32(SDHCI_BUFFER, Word);
    }
    {
        UINT32 Elapsed = 0;

        while ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_DATA_END) == 0) {
            if ((SdhciRead32(SDHCI_INT_STATUS) & SDHCI_INT_ERROR) != 0 || Elapsed >= 30000u) {
                /* 数据全喂完了却没有收尾：卡侧背板写没完成 */
                WifiBootMarkHexadecimal32("Boot: Pi WIFI no data end words=", Words);
                HalSerialBootMarkForce("\n");
                WifiSdioProgrammedIoFailCleanup();
                return -1;
            }
            SdhciDelayMicroseconds(100);
            Elapsed++;
        }
        SdhciWrite32(SDHCI_INT_STATUS, SDHCI_INT_DATA_END | SDHCI_INT_SPACE_AVAIL);
    }
    return 0;
}
