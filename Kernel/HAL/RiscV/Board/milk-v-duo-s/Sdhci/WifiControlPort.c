/*
 * WifiControlPort.c — PR-34：ME_SET_CONTROL_PORT_REQ(0x1404) → CFM(0x1405)
 *
 * 用户眼里：`wififw join` 关联成功之后紧跟的一行
 *           `WiFi: control port open (vif=0 ap=0)`，以及"以前为什么收不到 M1"的答案。
 * 从哪读：AicControlPortLayoutCheck（布局自检）→ AicControlPortBuild（2 B 参数）
 *         → AicControlPortSend（拼 + 发）。**等 CFM 不在这里**：CFM 与 M1 走同一条
 *         SDIO FIFO，而控制端口一开固件就可能先吐排队已久的 M1、后写 CFM，所以在
 *         WifiHandshake.c 里用同一条收包路径一起等（见 gAicEapolControlPortCfm）。
 * 别改：参数是厂商 lmac_msg.h:1757-1763 的逐字段镜像（sta_idx 在前、
 *       control_port_open 在后，bool_l 是 1 B）；发送前用编译出来的 sizeof/offsetof
 *       核对，排歪了就拒绝发送。
 * 为什么必须有这一刀：EAPOL 在固件眼里是"受控端口"上的流量，不开就只把广播帧交给
 *       主机（beacon 收得到、M1 收不到 —— 真机现象正是如此）。厂商在 sm_connect_ind
 *       之后立刻发这条（真机 `>>> rwnx_send_me_set_control_port_req()`，回 CFM 0x1405），
 *       我们照抄顺序：IND 成功 → 打开 → 马上盯数据帧。
 */
#include "WifiAssocPrivate.h"
#include "Compiler.h" /* BLOCKS_OFFSETOF：与 join/scan/bringup 同一套布局自检 */

/* 2 B 参数；UINT32 数组只为对齐（镜像里全是 1 B 字段，没有对齐要求） */
static UINT32 sAicControlPort[(AIC_CONTROL_PORT_LENGTH + 3u) / 4u];

typedef struct {
    const char *Name;
    UINT32 Actual;
    UINT32 Expected;
} AIC_CONTROL_PORT_LAYOUT_ITEM;

/* 期望值全部来自宿主 gcc include 厂商 lmac_msg.h 的编译输出（见报告 §1 的字段表） */
static const AIC_CONTROL_PORT_LAYOUT_ITEM sAicControlPortLayout[] = {
    {"me_set_control_port_req", (UINT32)sizeof(AIC_ME_CONTROL_PORT), AIC_CONTROL_PORT_LENGTH},
    {"me_set_control_port_req.sta_idx",
     (UINT32)BLOCKS_OFFSETOF(AIC_ME_CONTROL_PORT, StaIndex), 0u},
    {"me_set_control_port_req.control_port_open",
     (UINT32)BLOCKS_OFFSETOF(AIC_ME_CONTROL_PORT, ControlPortOpen), 1u},
};

#define AIC_CONTROL_PORT_LAYOUT_COUNT \
    ((UINT32)(sizeof(sAicControlPortLayout) / sizeof(sAicControlPortLayout[0])))

/* 一条不匹配就停：镜像排歪了，发出去固件只会把 2 B 读成别的意思 */
int AicControlPortLayoutCheck(void) {
    UINT32 Index;
    int Bad = 0;

    for (Index = 0; Index < AIC_CONTROL_PORT_LAYOUT_COUNT; Index++) {
        const AIC_CONTROL_PORT_LAYOUT_ITEM *Item = &sAicControlPortLayout[Index];

        if (Item->Actual == Item->Expected) {
            continue;
        }
        SdhciMark("WiFi: control port struct layout BAD: ");
        SdhciMark(Item->Name);
        SdhciMarkHex(" got=", Item->Actual, 4);
        SdhciMarkHex(" want=", Item->Expected, 4);
        SdhciMark("\n");
        Bad = 1;
    }
    if (Bad != 0) {
        SdhciMark("WiFi: control port struct layout check failed, refuse to send\n");
        return -1;
    }
    return 0;
}

/* sta_idx = 固件给这个 AP 分配的索引（SM_CONNECT_IND.ap_idx），open = 1 表示 EAPOL 归主机 */
UINT32 AicControlPortBuild(UINT8 *Buffer, UINT8 StaIndex, UINT8 Open) {
    AIC_ME_CONTROL_PORT *Request = (AIC_ME_CONTROL_PORT *)Buffer;

    Request->StaIndex = StaIndex;
    Request->ControlPortOpen = Open;
    return (UINT32)sizeof(*Request);
}

/*
 * 只发不等：发 0x1404。CFM 由调用方在同一条收包路径里识别（gAicEapolControlPortCfm）。
 * 这样"控制端口一开、固件先吐 M1"的那几毫秒不会被丢帧。
 */
int AicControlPortSend(SDHCI_HOST *Host, UINT8 StaIndex) {
    UINT32 Length = AicControlPortBuild((UINT8 *)sAicControlPort, StaIndex,
                                        (UINT8)AIC_CONTROL_PORT_OPEN);

    if (AicControlPortLayoutCheck() != 0) {
        return -1;
    }
    SdhciMark("WiFi: control port req: sta_idx=");
    SdhciMarkDecimal("", StaIndex);
    SdhciMark(" open=");
    SdhciMarkDecimal("", AIC_CONTROL_PORT_OPEN);
    SdhciMark(" (ME_SET_CONTROL_PORT_REQ 0x1404)\n");
    if (AicSendMessageEx(Host, (UINT16)AIC_ME_SET_CONTROL_PORT_REQ, (UINT16)AIC_TASK_ME,
                         sAicControlPort, (UINT16)Length) != 0) {
        SdhciMark("WiFi: handshake FAILED at control port: ME_SET_CONTROL_PORT_REQ(0x1404)"
                  " send failed (SDIO tx)\n");
        return -1;
    }
    return 0;
}
