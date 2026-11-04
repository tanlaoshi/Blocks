/*
 * WifiPatchTableParse.c — AICBT patch 表解析（验 tag / 取 INF）
 *
 * 自 WifiPatchTable.c 拆出；行为不变。
 */
#include "Wifi/Private.h"

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
