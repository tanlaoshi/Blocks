/*
 * WifiPatchApply.c — patch 落地：patch_config_8800d80 的 4 读 10 写 + 100 条写 + 哨兵
 *
 * 从 WifiFw.c 原样搬出（纯搬家，函数体逐字节不动），行为零变化。
 */
#include "WifiPrivate.h"

static int AicPatchAddressWindowOk(const char *What, UINT32 Address) {
    if ((Address & 3u) != 0u) {
        SdhciMarkHex("WiFi: patch cfg: ", Address, 8);
        SdhciMark(" not 4-aligned (");
        SdhciMark(What);
        SdhciMark(")\n");
        return 0;
    }
    if (Address < AIC_RAM_LO || Address >= AIC_RAM_HI) {
        SdhciMarkHex("WiFi: patch cfg: ", Address, 8);
        SdhciMark(" outside RAM window 0x10000..0x20ffff (");
        SdhciMark(What);
        SdhciMark(")\n");
        return 0;
    }
    return 1;
}

/* 4 个读 + 判据（不写任何东西） */
static int AicPatchConfigReads(SDHCI_HOST *Host, AIC_PATCH_CFG *Config) {
    UINT32 k;
    for (k = 0; k < sizeof(*Config); k++) {
        ((UINT8 *)Config)[k] = 0;
    }
    Config->PairCount = 1u; /* patch_tbl_8800d80 在 u02 非 5G 下只有 1 对 */

    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x0198u, &Config->ConfigBase) != 0) {
        SdhciMark("WiFi: patch cfg: rd 00120198 FAIL (no config_base)\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x01A0u, &Config->StrBase) != 0) {
        SdhciMark("WiFi: patch cfg: rd 001201a0 FAIL (no str_base)\n");
        return -1;
    }
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x001Cu, &Config->Version) != 0) {
        SdhciMark("WiFi: patch cfg: rd 0012001c FAIL (no version)\n");
        return -1;
    }
    SdhciMarkHex("WiFi: patch cfg: config_base=", Config->ConfigBase, 8);
    SdhciMarkHex(" str_base=", Config->StrBase, 8);
    SdhciMarkHex(" ver=", Config->Version, 8);
    SdhciMark("\n");
    if (!AicPatchAddressWindowOk("str_base", Config->StrBase)) {
        return -1;
    }
    if (!AicPatchAddressWindowOk("config_base", Config->ConfigBase)) {
        return -1;
    }
    if (Config->Version > 0x06090100u) {
        if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR + 0x01A4u, &Config->PatchAddress) != 0) {
            SdhciMark("WiFi: patch cfg: rd 001201a4 FAIL (no patch_buff_base)\n");
            return -1;
        }
        SdhciMarkHex("WiFi: patch cfg: patch_buff_base=", Config->PatchAddress, 8);
        SdhciMark(" (new map)\n");
        /* 评审 P1-3：厂商会把它当 0 用，那更糟；这里直接失败，不许猜 */
        if (Config->PatchAddress == 0u || !AicPatchAddressWindowOk("patch_buff_base", Config->PatchAddress)) {
            SdhciMark("WiFi: patch cfg: patch_buff_base unusable, refuse (no guessing)\n");
            return -1;
        }
    } else {
        Config->PatchAddress = 0x0016F800u;
        SdhciMark("WiFi: patch cfg: old map (ver<=06090100), use 0016f800\n");
    }
    SdhciMark("WiFi: patch cfg: 4 reads ok (preflight, nothing written yet)\n");
    return 0;
}

/* 10 个写（aic8800d80_compat.c:196-248）：magic/pair 4 条 + block_size[4] + pair 2 条 */
static int AicPatchConfigWrites(SDHCI_HOST *Host, const AIC_PATCH_CFG *Config) {
    UINT32 k;

    if (AicMemWrite32(Host, Config->StrBase + 0x00u, 0x48435450u) != 0 ||
        AicMemWrite32(Host, Config->StrBase + 0x08u, 0x50544348u) != 0 ||
        AicMemWrite32(Host, Config->StrBase + 0x04u, Config->PatchAddress) != 0 ||
        AicMemWrite32(Host, Config->StrBase + 0x0Cu, Config->PairCount) != 0) {
        SdhciMark("WiFi: patch cfg: magic/pair write FAIL\n");
        return -1;
    }
    for (k = 0; k < 4u; k++) {
        if (AicMemWrite32(Host, Config->StrBase + 0x30u + k * 4u, 0u) != 0) {
            SdhciMarkDecimal("WiFi: patch cfg: block_size write FAIL k=", k);
            SdhciMark("\n");
            return -1;
        }
    }
    if (AicMemWrite32(Host, Config->PatchAddress + 0u, Config->ConfigBase + 0x00B4u) != 0 ||
        AicMemWrite32(Host, Config->PatchAddress + 4u, 0xF3010000u) != 0) {
        SdhciMark("WiFi: patch cfg: pair write FAIL\n");
        return -1;
    }
    SdhciMark("WiFi: patch cfg: 10 writes ok\n");
    return 0;
}

/* dry 打印用：把要做的 14 个 op 列出来 */
static void AicPatchConfigPrint(void) {
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

/* 评审 P1-6：写完必须读回。6 个哨兵，任一不等即失败 */
static int AicPatchSentinels(SDHCI_HOST *Host, const AIC_PATCH_CFG *Config) {
    UINT32 V = 0;
    (void)Config; /* 6 个哨兵地址是固定的；StrBase 的 magic 由 cfg 写路径自己验 */
    UINT32 Want = 0;
    UINT8 F[4];
    UINT32 Got = 0;
    UINT32 k;

    struct {
        UINT32 Address;
        UINT32 Want;
        const char *Name;
    } S[6];
    UINT32 N = 0;

    S[N].Address = 0x40500150u; S[N].Want = 1u;          S[N].Name = "INF reset";        N++;
    S[N].Address = 0x001E7294u; S[N].Want = 0x001E0000u; S[N].Name = "INF patch_addr";   N++;
    S[N].Address = 0x0020F600u; S[N].Want = 0xF0223201u; S[N].Name = "TRAP first";       N++;
    S[N].Address = 0x00201FC8u; S[N].Want = 5u;          S[N].Name = "MODE btmode";      N++;

    for (k = 0; k < N; k++) {
        if (AicMemRead(Host, S[k].Address, &V) != 0) {
            SdhciMark("WiFi: sentinel ");
            SdhciMark(S[k].Name);
            SdhciMark(" read FAIL\n");
            return -1;
        }
        if (V != S[k].Want) {
            SdhciMark("WiFi: sentinel ");
            SdhciMark(S[k].Name);
            SdhciMarkHex(" got=", V, 8);
            SdhciMarkHex(" want=", S[k].Want, 8);
            SdhciMark(" MISMATCH\n");
            return -1;
        }
    }
    /* adid 首字 == fw_adid.bin 首 4 字节 */
    if (AicFwReadChunk(AIC_FW_ADID, 0, F, 4u, &Got) != 0 || Got != 4u) {
        SdhciMark("WiFi: sentinel adid: cannot read file head\n");
        return -1;
    }
    Want = (UINT32)F[0] | ((UINT32)F[1] << 8) | ((UINT32)F[2] << 16) | ((UINT32)F[3] << 24);
    if (AicMemRead(Host, 0x00201940u, &V) != 0 || V != Want) {
        SdhciMarkHex("WiFi: sentinel adid head got=", V, 8);
        SdhciMarkHex(" want=", Want, 8);
        SdhciMark(" MISMATCH\n");
        return -1;
    }
    /* fmacfw 首字 == fmacfw.bin 首 4 字节 */
    if (AicFwReadChunk(AIC_FW_FMAC, 0, F, 4u, &Got) != 0 || Got != 4u) {
        SdhciMark("WiFi: sentinel fmacfw: cannot read file head\n");
        return -1;
    }
    Want = (UINT32)F[0] | ((UINT32)F[1] << 8) | ((UINT32)F[2] << 16) | ((UINT32)F[3] << 24);
    if (AicMemRead(Host, AIC_RAM_FMAC_FW_ADDR, &V) != 0 || V != Want) {
        SdhciMarkHex("WiFi: sentinel fmacfw head got=", V, 8);
        SdhciMarkHex(" want=", Want, 8);
        SdhciMark(" MISMATCH\n");
        return -1;
    }
    SdhciMark("WiFi: sentinels 6/6 ok\n");
    return 0;
}

/* dry：只解析表 + 打清单，一个字节不写、一条消息不发 */
int AicPatchDry(void) {
    UINT32 TableLength = 0;
    UINT32 Count = 0;

    SdhciMark("WiFi: patch dry start\n");
    if (AicFwLoadPatchTable(0, gAicFwBuffer, AIC_FW_CHUNK, &TableLength) != 0) {
        SdhciMark("WiFi: patch dry: table unusable\n");
        return -1;
    }
    if (AicPatchBuildPlan(gAicFwBuffer, TableLength, &Count) != 0) {
        SdhciMark("WiFi: patch dry: plan build FAILED\n");
        return -1;
    }
    AicPatchPrintPlan(Count);
    if (AicPatchSelfCheck() != 0) {
        SdhciMark("WiFi: patch plan end INVALID, nothing written\n");
        return -1;
    }
    AicPatchConfigPrint(); /* dry 只打印，不读不写 */
    SdhciMark("WiFi: patch plan end, nothing written\n");
    return 0;
}

/* apply：真写。必须先过 dry，失败即停，绝不续写 */
int AicPatchApply(SDHCI_HOST *Host, const UINT8 *Table, UINT32 Length) {
    UINT32 Count = 0;
    UINT32 i;
    UINT32 Done = 0;
    AIC_PATCH_CFG Config;

    /* 评审 P1-4：三态。DONE 不许重放；POISONED（上次写失败）也不许续写，必须重新 probe。 */
    if (gAicPatchState == AIC_PATCH_DONE) {
        SdhciMark("WiFi: patch already applied (this power cycle)\n");
        return -1;
    }
    if (gAicPatchState == AIC_PATCH_POISONED) {
        SdhciMark("WiFi: patch state POISONED by an earlier failure, re-probe before retry\n");
        return -1;
    }
    if (AicPatchBuildPlan(Table, Length, &Count) != 0) {
        SdhciMark("WiFi: patch apply: plan build FAILED\n");
        return -1;
    }
    if (AicPatchSelfCheck() != 0) {
        SdhciMark("WiFi: patch apply refused: plan INVALID\n");
        return -1;
    }
    /*
     * 评审 P1-2：先把 4 个 cfg 读做完（纯读）。这里是全链路最可能失败的一步，
     * 排在 100 条写之前 —— 不合法就 0 写退出。
     */
    if (AicPatchConfigReads(Host, &Config) != 0) {
        SdhciMark("WiFi: patch apply refused: cfg preflight FAILED (0 writes done)\n");
        return -1;
    }
    SdhciMarkDecimal("WiFi: patch apply start, ", Count);
    SdhciMark(" items\n");
    for (i = 0; i < Count; i++) {
        AIC_PLAN_ITEM *It = &gAicPlan[i];
        if (It->Kind == AIC_PLAN_DELAY) {
            SdhciDelayMicroseconds(It->Address);
            continue;
        }
        if (AicMemWrite32(Host, It->Address, It->Value) != 0) {
            SdhciMarkDecimal("WiFi: patch[", i);
            SdhciMark("] type=");
            SdhciMark(AicPatchTypeName(It->Type));
            SdhciMarkHex(" addr=", It->Address, 8);
            SdhciMarkHex(" val=", It->Value, 8);
            SdhciMark(" FAIL (stop, no resume)\n");
            SdhciMarkDecimal("WiFi: patch applied ", Done);
            SdhciMark("/");
            SdhciMarkDecimal("", Count);
            SdhciMark(" then stopped; state=POISONED (re-probe required)\n");
            gAicPatchState = AIC_PATCH_POISONED;
            return -1;
        }
        Done++;
        if ((Done % 16u) == 0u || Done == Count) {
            SdhciMarkDecimal("WiFi: patch progress ", Done);
            SdhciMark("/");
            SdhciMarkDecimal("", Count);
            SdhciMark(" ok\n");
        }
    }
    if (AicPatchConfigWrites(Host, &Config) != 0) {
        SdhciMark("WiFi: patch cfg FAILED (table writes done, cfg not)\n");
        gAicPatchState = AIC_PATCH_POISONED;
        return -1;
    }
    /* 评审 P1-6：没有读回的"成功"不算成功 */
    if (AicPatchSentinels(Host, &Config) != 0) {
        SdhciMark("WiFi: patch sentinels FAILED (chip does not hold what we wrote)\n");
        gAicPatchState = AIC_PATCH_POISONED;
        return -1;
    }
    gAicPatchState = AIC_PATCH_DONE;
    SdhciMarkDecimal("WiFi: patch applied: ", Count);
    SdhciMark("/");
    SdhciMarkDecimal("", Count);
    SdhciMark(" ok, state=DONE\n");
    return 0;
}
