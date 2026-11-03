/*
 * WifiPatchTable.c — patch 状态复位 / 类型名 / MODE 自检 / 计划打印
 *
 * 解析见 WifiPatchTableParse.c；写计划见 WifiPatchBuildPlan.c。
 */
#include "WifiPrivate.h"

/* 评审 P1-4：probe 成功后允许重新应用（掉电/重插卡后芯片里的东西已经不在了） */
void SdhciWifiPatchStateReset(void) {
    gAicPatchState = AIC_PATCH_NONE;
}

const char *AicPatchTypeName(UINT32 Type) {
    switch (Type) {
    case AICBT_PT_INF: return "INF";
    case AICBT_PT_TRAP: return "TRAP";
    case AICBT_PT_B4: return "TB4";
    case AICBT_PT_BTMODE: return "MODE";
    case AICBT_PT_PWRON: return "PWRON";
    case AICBT_PT_AF: return "TAF";
    case AICBT_PT_VER: return "VER";
    default: return "?";
    }
}

/*
 * MODE 自检（真机评审要求）：写完清单立刻验算，不合法就不许进 apply。
 *   btmode 不能是 0xFF（AICBT_MODE_NULL = 非法值）
 *   baud 不能是 0
 *   flowctrl 只能是 0/1
 */
int AicPatchSelfCheck(void) {
    if (gAicModeValue[3] == 0xFFu) {
        SdhciMark("WiFi: patch plan INVALID: btmode=000000ff (AICBT_MODE_NULL)\n");
        return -1;
    }
    if (gAicModeValue[5] == 0u) {
        SdhciMark("WiFi: patch plan INVALID: baud=0\n");
        return -1;
    }
    if (gAicModeValue[6] > 1u) {
        SdhciMarkHex("WiFi: patch plan INVALID: flowctrl=", gAicModeValue[6], 8);
        SdhciMark("\n");
        return -1;
    }
    SdhciMarkDecimal("WiFi: patch self-check ok (btmode=", gAicModeValue[3]);
    SdhciMarkDecimal(" baud=", gAicModeValue[5]);
    SdhciMarkDecimal(" fc=", gAicModeValue[6]);
    SdhciMark(")\n");
    return 0;
}

/* 打一遍清单（dry 用，也用于 apply 前的回显）：一条一行，序号两位 */
void AicPatchPrintPlan(UINT32 Count) {
    UINT32 i;
    UINT32 Mem = 0;
    UINT32 Config = 0;
    UINT32 Delay = 0;

    UINT32 PerType[7];
    UINT32 k;

    for (k = 0; k < 7u; k++) {
        PerType[k] = 0;
    }
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            Delay++;
            continue;
        }
        if (It->Kind == AIC_PLAN_CFG) {
            Config++;
        } else {
            Mem++;
        }
        if (It->Type < 7u) {
            PerType[It->Type]++;
        }
    }
    SdhciMarkDecimal("WiFi: patch plan: 0", Mem + Config);
    SdhciMark(" writes, sentinels=6\n");
    SdhciMarkDecimal("WiFi: patch plan: mem=", Mem);
    SdhciMarkDecimal(" cfg=", Config);
    SdhciMarkDecimal(" delay=", Delay);
    SdhciMarkDecimal(" (+14 cfg ops: rd=4 wr=10), items=", Count);
    SdhciMark("\n");
    SdhciMarkDecimal("WiFi: patch plan: per type inf=", PerType[AICBT_PT_INF]);
    SdhciMarkDecimal(" trap=", PerType[AICBT_PT_TRAP]);
    SdhciMarkDecimal(" tb4=", PerType[AICBT_PT_B4]);
    SdhciMarkDecimal(" mode=", PerType[AICBT_PT_BTMODE]);
    SdhciMarkDecimal(" pwron=", PerType[AICBT_PT_PWRON]);
    SdhciMarkDecimal(" taf=", PerType[AICBT_PT_AF]);
    SdhciMark("\n");
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            SdhciMarkDecimal("WiFi: patch[", i);
            SdhciMark("] kind=delay type=PWRON udelay=500\n");
            continue;
        }
        SdhciMarkDecimal("WiFi: patch[", i);
        SdhciMark("] ");
        if (It->Kind == AIC_PLAN_CFG) {
            /* 这些值来自厂商运行时覆盖（不是表值），表值留一列便于对账 */
            SdhciMark("kind=cfg src=vendor ");
        } else {
            SdhciMark("kind=mem ");
        }
        SdhciMark("type=");
        SdhciMark(AicPatchTypeName(It->Type));
        SdhciMarkHex(" addr=", It->Address, 8);
        SdhciMarkHex(" val=", It->Value, 8);
        if (It->Kind == AIC_PLAN_CFG) {
            SdhciMarkHex(" (table=", It->TableValue, 8);
            SdhciMark(")");
        }
        SdhciMark("\n");
    }
    /* patch_config_8800d80（aic8800d80_compat.c:114-248）的 14 个 op，地址是符号式的 */
    SdhciMark("WiFi: patch[cfg] kind=rd addr=00120198 -> config_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a0 -> str_base\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=0012001c -> fw_version\n");
    SdhciMark("WiFi: patch[cfg] kind=rd addr=001201a4 -> patch_buff_base (only if ver>06090100)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x00 val=48435450 (magic)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x08 val=50544348 (magic2)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x04 val=<patch_addr> (pair_start)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x0c val=<pair_cnt> (pair_count)\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=str+0x30..3c val=00000000 (block_size[0..3])\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x00 val=<config_base+0x00b4>\n");
    SdhciMark("WiFi: patch[cfg] kind=mem addr=pair+0x04 val=f3010000\n");
}
