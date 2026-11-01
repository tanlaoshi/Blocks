/*
 * WifiEapolLayout.c — EAPOL 收包路径布局自检（从 WifiEapol.c 拆出）
 */
#include "WifiEapolPrivate.h"
#include "Compiler.h"

typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_EAPOL_LAYOUT_ITEM;

static const AIC_EAPOL_LAYOUT_ITEM sAicEapolLayout[] = {
    {"hw_rxhdr", (UINT32)sizeof(AIC_RX_HW_HEADER), AIC_RX_HW_STRUCT_LENGTH},
    {"hw_rxhdr.rx_vect1", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Vector1), 12u},
    {"hw_rxhdr.rx_vect2", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Vector2), 28u},
    {"hw_rxhdr.status", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Status),
     AIC_RX_HW_STATUS_OFFSET},
    {"hw_rxhdr.flags", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Flags), AIC_RX_HW_FLAGS_OFFSET},
    {"hw_rxhdr.pattern", (UINT32)BLOCKS_OFFSETOF(AIC_RX_HW_HEADER, Pattern),
     AIC_RX_HW_PATTERN_OFFSET},
    {"eapol.key_data_offset", (UINT32)AIC_EAPOL_KEY_FIXED_LENGTH,
     (UINT32)WIFI_EAPOL_KEY_DATA_OFFSET},
};

#define AIC_EAPOL_LAYOUT_COUNT ((UINT32)(sizeof(sAicEapolLayout) / sizeof(sAicEapolLayout[0])))

int AicEapolLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_EAPOL_LAYOUT_COUNT; Index++) {
        const AIC_EAPOL_LAYOUT_ITEM *Item = &sAicEapolLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: eapol struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: eapol struct layout check failed, refuse to poll\n");
        return -1;
    }
    return 0;
}
