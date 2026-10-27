/*
 * Command.c — SDHOST 发令与等完成（bd-pi-sd）
 */
#include "Sdhost/Private.h"

UINT32 SdhostWaitCommandRegisterClear(UINT32 TimeoutMicroseconds) {
    UINT32 Value = 0;
    UINT32 n;
    UINT32 Steps = TimeoutMicroseconds / 10u + 1u;

    for (n = 0; n < Steps; n++) {
        Value = SdhostRead32(SDCMD);
        if ((Value & SDCMD_NEW_FLAG) == 0) {
            return Value;
        }
        SdhostDelayMicroseconds(10);
    }
    return Value;
}

int SdhostWaitTransferComplete(void) {
    UINT32 n;

    for (n = 0; n < 10000u; n++) {
        UINT32 Edm = SdhostRead32(SDEDM);
        UINT32 Fsm = Edm & SDEDM_FSM_MASK;

        if (Fsm == SDEDM_FSM_IDENTMODE || Fsm == SDEDM_FSM_DATAMODE) {
            return 0;
        }
        if (Fsm == SDEDM_FSM_READWAIT || Fsm == SDEDM_FSM_WRITESTART1 ||
            Fsm == SDEDM_FSM_READDATA) {
            SdhostWrite32(SDEDM, Edm | SDEDM_FORCE_DATA_MODE);
            return 0;
        }
        SdhostDelayMicroseconds(100);
    }
    SdhostDumpRegisters("Boot: SDHOST xfer wait timeout");
    return -1;
}

int SdhostTransferBlockProgrammedIo(UINT8 *Buffer) {
    UINT32 *Words = (UINT32 *)(void *)Buffer;
    int CopyWords = 512 / 4;
    UINT32 Hsts = 0;
    UINT32 Spins = 0;

    while (CopyWords > 0) {
        int Burst = CopyWords < (int)SDDATA_FIFO_PIO_BURST ? CopyWords
                                                          : (int)SDDATA_FIFO_PIO_BURST;
        UINT32 Edm = SdhostRead32(SDEDM);
        int Aval = (int)((Edm >> SDEDM_FIFO_FILL_SHIFT) & SDEDM_FIFO_FILL_MASK);
        int WordsNow;

        if (Aval < Burst) {
            int Fsm = (int)(Edm & SDEDM_FSM_MASK);
            if (Fsm != (int)SDEDM_FSM_READDATA && Fsm != (int)SDEDM_FSM_READWAIT &&
                Fsm != (int)SDEDM_FSM_READCRC) {
                Hsts = SdhostRead32(SDHSTS);
                if ((Hsts & SDHSTS_ERROR_MASK) != 0) {
                    break;
                }
            }
            /* 对齐 wait_transfer_complete：约 1s 仍无字则 dump，勿空转挂死 */
            if (++Spins > 100000u) {
                SdhostDumpRegisters("Boot: SDHOST PIO stall");
                return -1;
            }
            SdhostDelayMicroseconds(10);
            continue;
        }
        Spins = 0;
        WordsNow = Aval > CopyWords ? CopyWords : Aval;
        CopyWords -= WordsNow;
        while (WordsNow--) {
            *Words++ = SdhostRead32(SDDATA);
        }
    }

    Hsts = SdhostRead32(SDHSTS);
    if ((Hsts & (SDHSTS_CRC16_ERROR | SDHSTS_CRC7_ERROR | SDHSTS_FIFO_ERROR)) != 0) {
        gSdhostLastHostStatus = Hsts;
        return -1;
    }
    if ((Hsts & (SDHSTS_CMD_TIME_OUT | SDHSTS_REW_TIME_OUT)) != 0) {
        gSdhostLastHostStatus = Hsts;
        return -1;
    }
    return 0;
}

/*
 * CommandIndex + 可选单块读。ResponseType=RESPONSE_*；DataBuffer!=0 → CMD17 形读 512B。
 */
int SdhostCommandSend(UINT8 CommandIndex, UINT32 Argument, int ResponseType, UINT8 *DataBuffer) {
    UINT32 Sdcmd;
    UINT32 Sdhsts;
    UINT32 Edm;
    UINT32 Fsm;
    int UseBusy = 0;
    int Ret;

    Edm = SdhostRead32(SDEDM);
    Fsm = Edm & SDEDM_FSM_MASK;
    if (Fsm != SDEDM_FSM_IDENTMODE && Fsm != SDEDM_FSM_DATAMODE && CommandIndex != 12) {
        SdhostDumpRegisters("Boot: SDHOST FSM busy");
        return -1;
    }

    Sdcmd = SdhostWaitCommandRegisterClear(100000u);
    if ((Sdcmd & SDCMD_NEW_FLAG) != 0) {
        SdhostDumpRegisters("Boot: SDHOST SDCMD stuck");
        return -1;
    }

    Sdhsts = SdhostRead32(SDHSTS);
    if ((Sdhsts & SDHSTS_ERROR_MASK) != 0) {
        SdhostWrite32(SDHSTS, Sdhsts);
    }

    if (DataBuffer != 0) {
        SdhostWrite32(SDHBCT, 512u);
        SdhostWrite32(SDHBLC, 1u);
    }

    SdhostWrite32(SDARG, Argument);

    Sdcmd = (UINT32)CommandIndex & SDCMD_CMD_MASK;
    if (ResponseType == RESPONSE_NONE) {
        Sdcmd |= SDCMD_NO_RESPONSE;
    } else {
        if (ResponseType == RESPONSE_R2) {
            Sdcmd |= SDCMD_LONG_RESPONSE;
        }
        if (ResponseType == RESPONSE_R1B) {
            Sdcmd |= SDCMD_BUSYWAIT;
            UseBusy = 1;
        }
    }
    if (DataBuffer != 0) {
        Sdcmd |= SDCMD_READ_CMD;
    }

    SdhostWrite32(SDCMD, Sdcmd | SDCMD_NEW_FLAG);

    if (!UseBusy) {
        Sdcmd = SdhostWaitCommandRegisterClear(100000u);
        if ((Sdcmd & SDCMD_NEW_FLAG) != 0) {
            gSdhostLastCommand = Sdcmd;
            gSdhostLastHostStatus = SdhostRead32(SDHSTS);
            return -1;
        }
        if ((Sdcmd & SDCMD_FAIL_FLAG) != 0) {
            Sdhsts = SdhostRead32(SDHSTS);
            SdhostWrite32(SDHSTS, SDHSTS_ERROR_MASK);
            gSdhostLastCommand = Sdcmd;
            gSdhostLastHostStatus = Sdhsts;
            /* ACMD41/CMD1 OCR：CRC7 可忽略 */
            if (!((Sdhsts & SDHSTS_CRC7_ERROR) != 0 &&
                  (CommandIndex == 41 || CommandIndex == 1) &&
                  (Sdhsts & ~SDHSTS_CRC7_ERROR & SDHSTS_ERROR_MASK) == 0)) {
                return -1;
            }
        }
        if (ResponseType == RESPONSE_R2) {
            gSdhostResponse[3] = SdhostRead32(SDRSP0);
            gSdhostResponse[2] = SdhostRead32(SDRSP1);
            gSdhostResponse[1] = SdhostRead32(SDRSP2);
            gSdhostResponse[0] = SdhostRead32(SDRSP3);
        } else if (ResponseType != RESPONSE_NONE) {
            gSdhostResponse[0] = SdhostRead32(SDRSP0);
        }
    } else {
        /* R1b：等 BUSY_IRPT（SDHCFG_BUSY_IRPT_EN） */
        UINT32 n;
        for (n = 0; n < 10000u; n++) {
            Sdhsts = SdhostRead32(SDHSTS);
            if ((Sdhsts & SDHSTS_ERROR_MASK) != 0) {
                SdhostWrite32(SDHSTS, SDHSTS_ERROR_MASK);
                gSdhostLastHostStatus = Sdhsts;
                return -1;
            }
            if ((Sdhsts & SDHSTS_BUSY_IRPT) != 0) {
                SdhostWrite32(SDHSTS, SDHSTS_BUSY_IRPT);
                break;
            }
            SdhostDelayMicroseconds(100);
        }
        if (n >= 10000u) {
            gSdhostLastHostStatus = SdhostRead32(SDHSTS);
            return -1;
        }
        Sdcmd = SdhostWaitCommandRegisterClear(100000u);
        if ((Sdcmd & (SDCMD_NEW_FLAG | SDCMD_FAIL_FLAG)) != 0) {
            gSdhostLastCommand = Sdcmd;
            gSdhostLastHostStatus = SdhostRead32(SDHSTS);
            return -1;
        }
        gSdhostResponse[0] = SdhostRead32(SDRSP0);
    }

    if (DataBuffer != 0) {
        Ret = SdhostTransferBlockProgrammedIo(DataBuffer);
        if (Ret != 0) {
            return Ret;
        }
        return SdhostWaitTransferComplete();
    }
    return 0;
}

