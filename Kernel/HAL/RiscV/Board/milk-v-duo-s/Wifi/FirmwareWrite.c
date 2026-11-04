/*
 * WifiFirmwareWrite.c — wififw write confirm：逐阶段真写 + stack start
 *
 * 自 WifiFw.c 拆出（行数债）；行为不变。
 */
#include "Wifi/Private.h"

/*
 * MM_SET_STACK_START_REQ(0x7B)：告诉固件"主机的协议栈起来了"。
 * 厂商把它放在固件下载完之后的第一条（rwnx_main.c:6947），早于 MM_RESET；
 * 补发是实验性的：CFM 拿不到只打一行，不影响后续流程。
 */
static void ApplicationFirmwareSetStackStart(SDHCI_HOST *Host) {
    UINT8 Param[4];
    UINT8 Confirm[16];
    UINT32 ConfirmLength = 0;

    Param[0] = (UINT8)AIC_MM_STACK_START_ON;
    Param[1] = (UINT8)AIC_MM_EFUSE_INVALID;
    Param[2] = (UINT8)AIC_MM_VENDOR_INFO_5G_BIT;
    Param[3] = (UINT8)AIC_MM_FWTRACE_KEEP;
    if (AicSendMessageEx(Host, (UINT16)AIC_MM_SET_STACK_START_REQ, (UINT16)AIC_TASK_MM, Param,
                         (UINT16)sizeof(Param)) != 0) {
        SdhciMark("WiFi: stack start: msg send FAILED\n");
        return;
    }
    if (AicRecvMessageTimeout(Host, (UINT16)AIC_MM_SET_STACK_START_CFM, Confirm, sizeof(Confirm),
                              &ConfirmLength, 4u, AIC_CFM_TIMEOUT_MS) != 0) {
        SdhciMark("WiFi: stack start: no cfm (continue anyway)\n");
        return;
    }
    SdhciMarkHex("WiFi: stack start ok (5g=", (UINT32)Confirm[0], 2);
    SdhciMarkHex(" vendor=", (UINT32)(ConfirmLength > 1u ? Confirm[1] : 0u), 2);
    SdhciMark(")\n");
}

int WifiFirmwareWrite(SDHCI_HOST *Host) {
    UINT32 Index;
    UINT32 Size = 0;
    int Result;

    gWifiFwState = 0u;
    gWifiFwStageIndex = 0u;
    gWifiFwSent = 0u;
    SdhciMarkDecimal("WiFi: fw write CONFIRM buf=", AIC_FW_CHUNK);
    SdhciMark("\n");

    {
        UINT32 TableLength = 0;
        if (AicFwLoadPatchTable(Host, gAicFwBuffer, AIC_FW_CHUNK, &TableLength) != 0) {
            SdhciMark("WiFi: fw precheck failed (patch table), nothing written\n");
            gWifiFwState = 2u;
            return -1;
        }
    }
    for (Index = 0; Index < AIC_FW_STAGE_MAX; Index++) {
        Result = AicFwCheckStage(&gAicStages[Index], &Size);
        if (Result < 0) {
            gWifiFwState = 2u;
            gWifiFwStageIndex = Index;
            SdhciMark("WiFi: fw precheck failed, nothing written\n");
            return -1;
        }
    }
    for (Index = 0; Index < AIC_FW_STAGE_MAX; Index++) {
        AIC_FW_STAGE *Stage = &gAicStages[Index];
        gWifiFwStageIndex = Index;
        gWifiFwSent = 0u;
        Result = AicFwCheckStage(Stage, &Size);
        if (Result == 1) {
            SdhciMark("WiFi: stage=");
            SdhciMark(Stage->Name);
            SdhciMark(" skipped (optional, no file)\n");
            continue;
        }
        if (Result != 0) {
            return -1;
        }
        if (Stage->Kind == AIC_STAGE_PATCHTBL) {
            UINT32 TableLength = 0;
            FAT_FILE_STAT Stat;
            if (FileSystemFileStat(AIC_FW_PATCH_TABLE, &Stat) != FAT_OK || Stat.Size == 0u ||
                Stat.Size > AIC_FW_CHUNK ||
                AicFwReadChunk(AIC_FW_PATCH_TABLE, 0u, gAicFwBuffer, Stat.Size, &TableLength) != 0 ||
                TableLength != Stat.Size) {
                AicFwFail("patchtbl", 0u, "table-read");
                return -1;
            }
            if (AicPatchApply(Host, gAicFwBuffer, TableLength) != 0) {
                AicFwFail("patchtbl", 0u, "apply");
                return -1;
            }
            continue;
        }
        if (Stage->Kind == AIC_STAGE_START) {
            if (AicFwStartApp(Host, Stage->Address) != 0) {
                return -1;
            }
            AicFwWaitBootMessage(Host);
            continue;
        }
        if (AicFwPushStage(Host, Stage, Size) != 0) {
            return -1;
        }
    }
    gWifiFwState = 1u;
    SdhciMark("WiFi: fw state=done (all stages ok)\n");
    ApplicationFirmwareSetStackStart(Host);
    return 0;
}
