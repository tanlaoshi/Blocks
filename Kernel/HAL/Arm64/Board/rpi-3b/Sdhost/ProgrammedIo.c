/*
 * ProgrammedIo.c — SDHOST 单块 512B PIO 读/写（对表 Linux bcm2835_sdhost）
 */
#include "Sdhost/Private.h"

int SdhostTransferBlockProgrammedIoRead(UINT8 *Buffer) {
    UINT32 *Words = (UINT32 *)(void *)Buffer;
    int CopyWords = 512 / 4;
    UINT32 HostStatus = 0;
    UINT32 Spins = 0;

    while (CopyWords > 0) {
        int Burst = CopyWords < (int)SDDATA_FIFO_PIO_BURST ? CopyWords
                                                          : (int)SDDATA_FIFO_PIO_BURST;
        UINT32 Edm = SdhostRead32(SDEDM);
        int Available = (int)((Edm >> SDEDM_FIFO_FILL_SHIFT) & SDEDM_FIFO_FILL_MASK);
        int WordsNow;
        int FiniteState = (int)(Edm & SDEDM_FSM_MASK);

        if (Available < Burst) {
            if (FiniteState != (int)SDEDM_FSM_READDATA && FiniteState != (int)SDEDM_FSM_READWAIT &&
                FiniteState != (int)SDEDM_FSM_READCRC) {
                HostStatus = SdhostRead32(SDHSTS);
                if ((HostStatus & SDHSTS_ERROR_MASK) != 0) {
                    break;
                }
            }
            if (++Spins > 100000u) {
                SdhostDumpRegisters("Boot: SDHOST PIO read stall");
                return -1;
            }
            SdhostDelayMicroseconds(10);
            continue;
        }
        Spins = 0;
        WordsNow = Available > CopyWords ? CopyWords : Available;
        CopyWords -= WordsNow;
        while (WordsNow--) {
            *Words++ = SdhostRead32(SDDATA);
        }
    }

    HostStatus = SdhostRead32(SDHSTS);
    if ((HostStatus & (SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR | SDHSTS_FIFO_ERROR)) != 0) {
        gSdhostLastHostStatus = HostStatus;
        return -1;
    }
    if ((HostStatus & (SDHSTS_CMD_TIME_OUT | SDHSTS_REW_TIME_OUT)) != 0) {
        gSdhostLastHostStatus = HostStatus;
        return -1;
    }
    return 0;
}

int SdhostTransferBlockProgrammedIoWrite(const UINT8 *Buffer) {
    const UINT32 *Words = (const UINT32 *)(const void *)Buffer;
    int CopyWords = 512 / 4;
    UINT32 HostStatus = 0;
    UINT32 Spins = 0;

    while (CopyWords > 0) {
        int Burst = CopyWords < (int)SDDATA_FIFO_PIO_BURST ? CopyWords
                                                          : (int)SDDATA_FIFO_PIO_BURST;
        UINT32 Edm = SdhostRead32(SDEDM);
        int Fill = (int)((Edm >> SDEDM_FIFO_FILL_SHIFT) & SDEDM_FIFO_FILL_MASK);
        int Free = (int)SDDATA_FIFO_WORDS - Fill;
        int WordsNow;
        int FiniteState = (int)(Edm & SDEDM_FSM_MASK);

        if (Free < Burst) {
            /* 合法写态对表 U-Boot bcm2835_transfer_block_pio */
            if (FiniteState != (int)SDEDM_FSM_WRITEDATA &&
                FiniteState != (int)SDEDM_FSM_WRITEWAIT1 &&
                FiniteState != (int)SDEDM_FSM_WRITEWAIT2 &&
                FiniteState != (int)SDEDM_FSM_WRITECRC &&
                FiniteState != (int)SDEDM_FSM_WRITESTART1 &&
                FiniteState != (int)SDEDM_FSM_WRITESTART2) {
                HostStatus = SdhostRead32(SDHSTS);
                if ((HostStatus & SDHSTS_ERROR_MASK) != 0) {
                    gSdhostLastHostStatus = HostStatus;
                    SdhostDumpRegisters("Boot: SDHOST PIO write hsts");
                    return -1;
                }
            }
            if (++Spins > 100000u) {
                SdhostDumpRegisters("Boot: SDHOST PIO write stall");
                return -1;
            }
            SdhostDelayMicroseconds(10);
            continue;
        }
        Spins = 0;
        WordsNow = Free > CopyWords ? CopyWords : Free;
        CopyWords -= WordsNow;
        while (WordsNow--) {
            SdhostWrite32(SDDATA, *Words++);
        }
    }

    HostStatus = SdhostRead32(SDHSTS);
    if ((HostStatus & (SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR | SDHSTS_FIFO_ERROR)) != 0) {
        gSdhostLastHostStatus = HostStatus;
        return -1;
    }
    if ((HostStatus & (SDHSTS_CMD_TIME_OUT | SDHSTS_REW_TIME_OUT)) != 0) {
        gSdhostLastHostStatus = HostStatus;
        return -1;
    }
    return 0;
}
