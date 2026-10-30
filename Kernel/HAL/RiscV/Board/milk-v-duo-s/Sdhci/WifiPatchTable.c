/*
 * WifiPatchTable.c — patch 表解析与写计划：解析 fw_patch_table.bin、先算后写
 *
 * 从 WifiFw.c 原样搬出（纯搬家，函数体逐字节不动），行为零变化。
 */
#include "WifiPrivate.h"

/* 评审 P1-4：probe 成功后允许重新应用（掉电/重插卡后芯片里的东西已经不在了） */
void SdhciWifiPatchStateReset(void) {
    gAicPatchState = AIC_PATCH_NONE;
}

/* 表里那个 16 B 名字是不是 Want（允许后面补 0） */
static int AicPatchNameIs(const UINT8 *Name, const char *Want) {
    UINT32 i;

    for (i = 0; i < AICBT_PT_NAME; i++) {
        char C = Want[i];
        if (C == 0) {
            return (Name[i] == 0u) ? 1 : 0;
        }
        if ((char)Name[i] != C) {
            return 0;
        }
    }
    return 1;
}

/*
 * 解析 patch 表（厂商 aicbt_patch_table_alloc，aic_bsp_driver.c:1295-1352）：
 *   [0..15]   "AICBT_PT_TAG"
 *   循环： [16] name / [16..19] type / [20..23] len(以 8 字节为单位) / data[len*8]
 *
 * 只做两件事：验 tag、把类型 0（AICBT_PT_INF）的地址信息取出来。
 * tag 不对就明确失败 —— 绝不"猜着用"。
 */
int AicPatchTableParse(const UINT8 *Buffer, UINT32 Length, AIC_PATCH_INFO *Info) {
    UINT32 Off = AICBT_PT_HDR;
    UINT32 Type;
    UINT32 EntryLength;
    UINT32 i;
    UINT32 Found = 0;

    for (i = 0; i < (UINT32)sizeof(*Info); i++) {
        ((UINT8 *)Info)[i] = 0;
    }
    if (Buffer == 0 || Length < AICBT_PT_HDR + 24u) {
        SdhciMarkHex("WiFi: pt too short len=", Length, 4);
        SdhciMark("\n");
        return -1;
    }
    /*
     * 评审 P0-3：厂商比的是 sizeof(AICBT_PT_TAG) = 13（12 字符 + 结尾 0），
     * 以前这里只比 12 B，比厂商更松。现在统一成 13 B。
     */
    for (i = 0; i < sizeof(AICBT_PT_TAG); i++) {
        if (Buffer[i] != (UINT8)AICBT_PT_TAG[i]) {
            SdhciMark("WiFi: pt tag BAD (want AICBT_PT_TAG + NUL, 13B)\n");
            return -1;
        }
    }
    while (Off < Length) {
        /* 尾部 < 24 B 不许静默丢弃（厂商 aic_bsp_driver.c:1314 是 < size 继续） */
        if (Length - Off < 24u) {
            SdhciMarkHex("WiFi: pt trailing bytes ", Length - Off, 4);
            SdhciMark(" (tail < 24) -> BAD\n");
            return -1;
        }
        Type = (UINT32)Buffer[Off + 16u] | ((UINT32)Buffer[Off + 17u] << 8) |
               ((UINT32)Buffer[Off + 18u] << 16) | ((UINT32)Buffer[Off + 19u] << 24);
        EntryLength = (UINT32)Buffer[Off + 20u] | ((UINT32)Buffer[Off + 21u] << 8) |
                   ((UINT32)Buffer[Off + 22u] << 16) | ((UINT32)Buffer[Off + 23u] << 24);
        if (Type >= 1000u) {
            EntryLength = 0; /* 厂商的临时兼容：类型 >=1000 时长度按 0 处理 */
        }
        /*
         * 评审 P0-3：EntryLength 来自文件，`Off + 24 + EntryLength*8` 在 32 位下会回绕
         * （EntryLength >= 0x20000000 时乘积为 0），随后按 8 B 步长读出缓冲区外，
         * 拿到"对齐的野地址"。必须**先除后乘**判断，不能先乘再比。
         */
        if (EntryLength > (Length - Off - 24u) / 8u) {
            SdhciMarkHex("WiFi: pt entry overruns, len=", EntryLength, 4);
            SdhciMarkHex(" at off=", Off, 8);
            SdhciMark("\n");
            return -1;
        }
        if (Type == AICBT_PT_INF) {
            if (Found != 0) {
                SdhciMark("WiFi: pt duplicate INF record\n");
                return -1;
            }
            if (AicPatchNameIs(&Buffer[Off], "AICBT_PINF_T") == 0) {
                SdhciMark("WiFi: pt info tuple name BAD (want AICBT_PINF_T)\n");
                return -1;
            }
            Info->InfoLength = EntryLength;
            if (EntryLength == 0u) {
                SdhciMark("WiFi: pt info_len=0 (D80 requires != 0)\n");
                return -1;
            }
            {
                const UINT8 *D = &Buffer[Off + 24u];
                UINT32 W[8];
                UINT32 k;
                UINT32 MaxW = EntryLength * 2u;
                if (MaxW > 8u) {
                    MaxW = 8u;
                }
                for (k = 0; k < MaxW; k++) {
                    W[k] = (UINT32)D[k * 4u] | ((UINT32)D[k * 4u + 1u] << 8) |
                           ((UINT32)D[k * 4u + 2u] << 16) | ((UINT32)D[k * 4u + 3u] << 24);
                }
                for (k = MaxW; k < 8u; k++) {
                    W[k] = 0;
                }
                /* 顺序照 aicbt_patch_info_unpack：从 adid_addrinf 起连续拷进去 */
                Info->AdidAddressInfo = W[0];
                Info->AddressAdid = W[1];
                Info->PatchAddressInfo = W[2];
                Info->AddressPatch = W[3];
                Info->ResetAddress = W[4];
                Info->ResetValue = W[5];
                Info->AdidFlagAddress = W[6];
                Info->AdidFlag = W[7];
            }
            /*
             * 评审 P0-3：不在这里 return —— 必须把整张表走完，
             * 否则尾部/结构损伤会被"读完 INF 就走"掩盖。
             */
            Found = 1;
        }
        Off += 24u + EntryLength * 8u;
    }
    if (Found == 0) {
        SdhciMark("WiFi: pt no info tuple found\n");
        return -1;
    }
    if (Off != Length) {
        SdhciMarkHex("WiFi: pt walk ended at ", Off, 8);
        SdhciMarkHex(" but file len=", Length, 8);
        SdhciMark("\n");
        return -1;
    }
    return 0;
}

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
