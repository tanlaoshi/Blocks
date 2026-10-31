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

    /* 对表 U-Boot bcm2835_wait_transfer_complete：仅在读卡住态 FORCE */
    for (n = 0; n < 10000u; n++) {
        UINT32 Edm = SdhostRead32(SDEDM);
        UINT32 FiniteState = Edm & SDEDM_FSM_MASK;

        if (FiniteState == SDEDM_FSM_IDENTMODE || FiniteState == SDEDM_FSM_DATAMODE) {
            return 0;
        }
        if (FiniteState == SDEDM_FSM_READWAIT || FiniteState == SDEDM_FSM_WRITESTART1 ||
            FiniteState == SDEDM_FSM_READDATA) {
            SdhostWrite32(SDEDM, Edm | SDEDM_FORCE_DATA_MODE);
            return 0;
        }
        SdhostDelayMicroseconds(100);
    }
    SdhostDumpRegisters("Boot: SDHOST xfer wait timeout");
    return -1;
}

/*
 * CommandIndex + 可选单块数据。DataBuffer!=0 时 IsWrite 选 READ_CMD / WRITE_CMD。
 */
int SdhostCommandSend(UINT8 CommandIndex, UINT32 Argument, int ResponseType, UINT8 *DataBuffer,
                      int IsWrite) {
    UINT32 Sdcmd;
    UINT32 HostStatus;
    UINT32 Edm;
    UINT32 FiniteState;
    int UseBusy = 0;
    int Ret;

    Edm = SdhostRead32(SDEDM);
    FiniteState = Edm & SDEDM_FSM_MASK;
    if (FiniteState != SDEDM_FSM_IDENTMODE && FiniteState != SDEDM_FSM_DATAMODE &&
        CommandIndex != 12) {
        SdhostDumpRegisters("Boot: SDHOST FSM busy");
        return -1;
    }

    Sdcmd = SdhostWaitCommandRegisterClear(100000u);
    if ((Sdcmd & SDCMD_NEW_FLAG) != 0) {
        SdhostDumpRegisters("Boot: SDHOST SDCMD stuck");
        return -1;
    }

    HostStatus = SdhostRead32(SDHSTS);
    if ((HostStatus & SDHSTS_ERROR_MASK) != 0) {
        SdhostWrite32(SDHSTS, HostStatus);
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
        if (IsWrite) {
            Sdcmd |= SDCMD_WRITE_CMD;
        } else {
            Sdcmd |= SDCMD_READ_CMD;
        }
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
            HostStatus = SdhostRead32(SDHSTS);
            SdhostWrite32(SDHSTS, SDHSTS_ERROR_MASK);
            gSdhostLastCommand = Sdcmd;
            gSdhostLastHostStatus = HostStatus;
            if (!((HostStatus & SDHSTS_CRC7_ERROR) != 0 &&
                  (CommandIndex == 41 || CommandIndex == 1) &&
                  (HostStatus & ~SDHSTS_CRC7_ERROR & SDHSTS_ERROR_MASK) == 0)) {
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
        UINT32 n;
        for (n = 0; n < 10000u; n++) {
            HostStatus = SdhostRead32(SDHSTS);
            if ((HostStatus & SDHSTS_ERROR_MASK) != 0) {
                SdhostWrite32(SDHSTS, SDHSTS_ERROR_MASK);
                gSdhostLastHostStatus = HostStatus;
                return -1;
            }
            if ((HostStatus & SDHSTS_BUSY_IRPT) != 0) {
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
        if (IsWrite) {
            Ret = SdhostTransferBlockProgrammedIoWrite(DataBuffer);
        } else {
            Ret = SdhostTransferBlockProgrammedIoRead(DataBuffer);
        }
        if (Ret != 0) {
            return Ret;
        }
        return SdhostWaitTransferComplete();
    }
    return 0;
}
