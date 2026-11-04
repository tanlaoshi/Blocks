/*
 * WifiPatchBuildPlan.c — AICBT patch 写计划摊平与条数自检
 *
 * 自 WifiPatchTable.c 拆出；行为不变。
 */
#include "Wifi/Private.h"

/* 越界检查：不许写主固件区（0x00120000 起 331348 B），不许非对齐 */
static int AicPatchAddressOk(UINT32 Address) {
    if ((Address & 3u) != 0u) {
        return 0;
    }
    if (Address >= AIC_RAM_FMAC_FW_ADDR && Address < AIC_RAM_FMAC_FW_ADDR + AIC_D80_FMAC_SIZE) {
        return 0;
    }
    return 1;
}

/* 把表里的 7 条记录摊平成计划（顺序 = 厂商 for 循环顺序，不重排） */
int AicPatchBuildPlan(const UINT8 *Table, UINT32 Length, UINT32 *OutCount) {
    UINT32 Off = AICBT_PT_HDR;
    UINT32 Type;
    UINT32 EntryLength;
    UINT32 i;
    UINT32 N = 0;
    UINT32 PerTypeGot[7];
    UINT32 ModeRecs = 0;
    UINT32 k2;

    /* 评审 P1-5：gAicModeValue 是 static，表里 MODE 少于 9 对时会读到上一次的残留 */
    for (k2 = 0; k2 < 9u; k2++) {
        gAicModeValue[k2] = 0u;
    }
    for (k2 = 0; k2 < 7u; k2++) {
        PerTypeGot[k2] = 0u;
    }
    while (Off < Length) {
        if (Length - Off < 24u) {
            SdhciMarkHex("WiFi: patch plan: trailing bytes ", Length - Off, 4);
            SdhciMark(" (tail < 24)\n");
            return -1;
        }
        Type = (UINT32)Table[Off + 16u] | ((UINT32)Table[Off + 17u] << 8) |
               ((UINT32)Table[Off + 18u] << 16) | ((UINT32)Table[Off + 19u] << 24);
        EntryLength = (UINT32)Table[Off + 20u] | ((UINT32)Table[Off + 21u] << 8) |
                   ((UINT32)Table[Off + 22u] << 16) | ((UINT32)Table[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLength = 0;
        }
        /* 评审 P0-3：先除后乘，杜绝 EntryLength*8 的 32 位回绕 */
        if (EntryLength > (Length - Off - 24u) / 8u) {
            SdhciMarkHex("WiFi: patch plan: entry overruns, len=", EntryLength, 4);
            SdhciMarkHex(" at off=", Off, 8);
            SdhciMark("\n");
            return -1;
        }
        if (Type < 7u) {
            PerTypeGot[Type] += EntryLength;
        }
        if (Type == AICBT_PT_BTMODE) {
            ModeRecs++;
        }
        if (Type == AICBT_PT_VER) {
            char Ver[72];
            UINT32 k;
            UINT32 NChars = EntryLength * 8u;
            if (NChars > (UINT32)sizeof(Ver) - 1u) {
                NChars = (UINT32)sizeof(Ver) - 1u;
            }
            for (k = 0; k < NChars; k++) {
                UINT8 C = Table[Off + 24u + k];
                Ver[k] = (C >= 0x20u && C < 0x7Fu) ? (char)C : '.';
            }
            Ver[NChars] = 0;
            SdhciMark("WiFi: patch ver=\"");
            SdhciMark(Ver);
            SdhciMark("\" (print only, no write)\n");
            Off += 24u + EntryLength * 8u;
            continue;
        }
        for (i = 0; i < EntryLength; i++) {
            const UINT8 *P = &Table[Off + 24u + i * 8u];
            UINT32 Address = (UINT32)P[0] | ((UINT32)P[1] << 8) | ((UINT32)P[2] << 16) |
                          ((UINT32)P[3] << 24);
            UINT32 Value = (UINT32)P[4] | ((UINT32)P[5] << 8) | ((UINT32)P[6] << 16) |
                         ((UINT32)P[7] << 24);
            UINT32 TableValue = Value; /* 表里原值，覆盖后仍保留一列用于对账 */
            UINT8 Kind = AIC_PLAN_MEM;

            if (Type == AICBT_PT_BTMODE) {
                /*
                 * 厂商 aic_bsp_driver.c:1481-1493 覆盖的是 **pair 的 value**：
                 * data 是 u32*，每对是 (data[0]=addr, data[1]=val)，所以
                 *   *(data+1)=hwinfo<0  *(data+3)=hwinfo  *(data+5)=cpmode
                 *   *(data+7)=btmode  *(data+9)=btport  *(data+11)=baud
                 *   *(data+13)=flowctrl  *(data+15)=lpm  *(data+17)=txpwr
                 * 换算成 pair 下标就是 0..8 —— 不是 1/3/5/…/17。
                 * 上一版把字下标当 pair 下标，9 条整体错位（btmode 槽位写进了 0xffffffff）。
                 */
                switch (i) {
                case 0u: Value = 1u; break; /* hwinfo < 0 → true（hwinfo 未知） */
                case 1u: Value = AIC_BT_HWINFO; break;
                case 2u: Value = AIC_BT_CPMODE; break;
                case 3u: Value = AIC_BT_BTMODE; break;
                case 4u: Value = AIC_BT_BTPORT; break;
                case 5u: Value = AIC_BT_BAUD; break;
                case 6u: Value = AIC_BT_FLOWCTRL; break;
                case 7u: Value = AIC_BT_LPM; break;
                case 8u: Value = AIC_BT_TXPWR; break;
                default: break;
                }
                if (i < 9u) {
                    gAicModeValue[i] = Value;
                }
                Kind = AIC_PLAN_CFG;
            }
            if (AicPatchAddressOk(Address) == 0) {
                SdhciMarkHex("WiFi: patch plan: bad addr ", Address, 8);
                SdhciMarkHex(" at entry off=", Off, 8);
                SdhciMark("\n");
                return -1;
            }
            if (N >= AIC_PLAN_MAX - 2u) {
                SdhciMark("WiFi: patch plan overflow\n");
                return -1;
            }
            gAicPlan[N].Kind = Kind;
            gAicPlan[N].Type = (UINT8)Type;
            gAicPlan[N].Address = Address;
            gAicPlan[N].Value = Value;
            gAicPlan[N].TableValue = TableValue;
            N++;
        }
        if (Type == AICBT_PT_PWRON) {
            gAicPlan[N].Kind = AIC_PLAN_DELAY;
            gAicPlan[N].Type = (UINT8)Type;
            gAicPlan[N].Address = 500u;
            gAicPlan[N].Value = 0u;
            N++;
        }
        Off += 24u + EntryLength * 8u;
    }
    if (Off != Length) {
        SdhciMarkHex("WiFi: patch plan: walk ended at ", Off, 8);
        SdhciMarkHex(" len=", Length, 8);
        SdhciMark("\n");
        return -1;
    }
    /*
     * 评审 P0-2：条数自检。表被截断时（例如只读到 768 B）解析会"成功"但少几百条，
     * 以前照样 apply。这里把按类型的小计钉死在 u02 的画像上，不一致就 INCOMPLETE。
     */
    {
        static const UINT32 Want[7] = {4u, 19u, 45u, 9u, 3u, 20u, 8u};
        static const char *KindName[7] = {"inf", "trap", "tb4", "mode", "pwron", "taf", "ver"};
        for (k2 = 0; k2 < 7u; k2++) {
            if (PerTypeGot[k2] != Want[k2]) {
                SdhciMark("WiFi: patch plan INCOMPLETE: ");
                SdhciMark(KindName[k2]);
                SdhciMarkDecimal(" got=", PerTypeGot[k2]);
                SdhciMarkDecimal(" want=", Want[k2]);
                SdhciMark("\n");
                return -1;
            }
        }
        if (ModeRecs != 1u) {
            SdhciMarkDecimal("WiFi: patch plan INCOMPLETE: MODE records=", ModeRecs);
            SdhciMark(" (want 1)\n");
            return -1;
        }
        if (N != 1u + 4u + 19u + 45u + 9u + 3u + 20u) {
            SdhciMarkDecimal("WiFi: patch plan INCOMPLETE: items=", N);
            SdhciMark(" (want 101)\n");
            return -1;
        }
    }
    *OutCount = N;
    return 0;
}
