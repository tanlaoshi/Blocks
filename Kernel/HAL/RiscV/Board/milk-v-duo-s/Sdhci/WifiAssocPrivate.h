/*
 * WifiAssocPrivate.h — PR-32：SM_CONNECT(0x1800) 发起关联的内部声明
 *
 * 用户眼里：`wififw join <ssid> [bssid]` 发出去的那个包长什么样、字段从哪来。
 * 从哪读：AicJoinBuild / AicJoinLayoutCheck / SdhciWifiJoin（WifiAssoc.c）。
 * 别改：下面三个结构体是厂商 lmac_msg.h 的**逐字段镜像**（连编译器会插的填充都显式
 *       写出），字段顺序/宽度/偏移必须与厂商一致；AicJoinLayoutCheck 在发送前核对
 *       sizeof/offsetof，排歪了就拒绝发送。长度不是手写数字，是厂商头文件编译出来的
 *       sizeof（Archive/Duo-WiFi-关联与WPA2调研.md §2 有复现命令和输出）。
 * 来源：Source/DUO/sdk/duo-buildroot-sdk-v2-2.0.1/linux_5.10/drivers/net/wireless/
 *       aicsemi/aic8800/aic8800_fdrv/{lmac_msg.h,lmac_mac.h}
 *       + 目标编译器实测 sizeof/offsetof（宿主 gcc -I 最小 linux 桩，同 WifiConfigMessage.c 的做法）。
 */
#ifndef SDHCI_WIFI_ASSOC_PRIVATE_H
#define SDHCI_WIFI_ASSOC_PRIVATE_H

#include "WifiScanPrivate.h"

/*
 * ---- 消息 id ----
 * 全部由**编译器数厂商 enum**得到（不是手数）：
 *   lmac_msg.h:32-52   task 枚举：TASK_MM=0 → TASK_SM=6
 *   lmac_msg.h:123     LMAC_FIRST_MSG(task) = task << 10
 *   lmac_msg.h:2184-2213 sm_msg_tag：SM_CONNECT_REQ 是第一项 ⇒ 0x1800，CFM/IND 紧随
 *   lmac_msg.h:210     MM_KEY_ADD_REQ 是 mm_msg_tag 第 36 项 ⇒ 0x0024，CFM 0x0025
 * 复现：见调研文档 §2（probe.c 打印这些宏的数值）。
 */
#define AIC_TASK_SM 6u
#define AIC_SM_CONNECT_REQ 0x1800u
#define AIC_SM_CONNECT_CFM 0x1801u
#define AIC_SM_CONNECT_IND 0x1802u
/* 下一刀（断开/失败取原因）会用到；本刀只登记 id，不发 */
#define AIC_SM_DISCONNECT_REQ 0x1803u
#define AIC_SM_DISCONNECT_CFM 0x1804u
#define AIC_SM_DISCONNECT_IND 0x1805u
/*
 * 下一刀装钥匙（本刀**不发**，只在这里登记 id，免得下刀再去数一遍）：
 * 厂商 rwnx_msg_tx.c:649 rwnx_msg_zalloc(MM_KEY_ADD_REQ, TASK_MM, ...)；
 * 字段表（偏移/大小）见调研文档 §5，sizeof(struct mm_key_add_req)=44。
 */
#define AIC_MM_KEY_ADD_REQ 0x0024u
#define AIC_MM_KEY_ADD_CFM 0x0025u

/*
 * ---- 控制端口（PR-34）----
 * struct me_set_control_port_req（lmac_msg.h:1757-1763）只有两个 1 B 字段：
 *   sta_idx@0、control_port_open@1（bool_l 在 lmac_types.h:51 就是 bool ⇒ 1 B）
 * ⇒ sizeof = 2。宿主 gcc include 厂商头**编译打印**出来的实测值（报告 §1）：
 *   sizeof(me_set_control_port_req) = 2
 *   offsetof(sta_idx) = 0、offsetof(control_port_open) = 1
 * CFM 没有参数（rwnx_msg_tx.c:2071 `rwnx_send_msg(..., ME_SET_CONTROL_PORT_CFM, NULL)`），
 * 所以只认 id、不读字段。
 */
#define AIC_CONTROL_PORT_LENGTH 2u
#define AIC_CONTROL_PORT_OPEN 1u
#define AIC_CONTROL_PORT_CLOSE 0u

typedef struct {
    UINT8 StaIndex;        /* +0  用 SM_CONNECT_IND.ap_idx（lmac_msg.h:1760） */
    UINT8 ControlPortOpen; /* +1  bool_l：1 = EAPOL 交给主机 */
} AIC_ME_CONTROL_PORT;

/* ---- 参数长度：厂商 sizeof（§2 的编译输出） ---- */
#define AIC_SM_CONNECT_LENGTH 320u      /* ssid33 + bssid6 + chan6 + 4+2+2+2+1+1+1+1 + 2 + ie_buf256 */
#define AIC_SM_CONNECT_CFM_LENGTH 1u    /* struct sm_connect_cfm { u8 status; } */
#define AIC_SM_CONNECT_IND_LENGTH 852u  /* 到 ac_param[4] 末尾，见 AIC_SM_CONNECT_INDICATION */

/* ---- 本刀填的值 ---- */
#define AIC_JOIN_SSID_MAX 32u        /* MAC_SSID_LEN（lmac_mac.h:49） */
#define AIC_JOIN_MAC_LENGTH 6u       /* MAC_ADDR_LEN（lmac_mac.h:40） */
#define AIC_JOIN_BAND_2G4 0u         /* PHY_BAND_2G4（lmac_mac.h:69） */
/*
 * auth_type：厂商的映射表在 rwnx_msg_tx.c:2428-2440（if 链），值本身在
 * linux_5.10/include/linux/ieee80211.h:2481-2488：
 *   NL80211_AUTHTYPE_AUTOMATIC / OPEN_SYSTEM → WLAN_AUTH_OPEN(0)
 *   NL80211_AUTHTYPE_SHARED_KEY             → WLAN_AUTH_SHARED_KEY(1)
 *   NL80211_AUTHTYPE_FT                     → WLAN_AUTH_FT(2)
 *   NL80211_AUTHTYPE_SAE                    → WLAN_AUTH_SAE(3)
 *
 * **WPA2-PSK 的 auth_type 就是 0（OPEN），不是 1。** wpa_supplicant 对 WPA2-PSK
 * 用的 auth_alg 是 WPA_AUTH_ALG_OPEN，cfg80211 交给厂商的 sme->auth_type 是
 * NL80211_AUTHTYPE_OPEN_SYSTEM；1 是 WEP 的共享密钥认证，发给 WPA2 AP 会被直接拒
 * （status_code=12 的一种来源）。WPA2 的身份由 RSN IE + WPA_WPA2_IN_USE 表达。
 */
#define AIC_JOIN_AUTH_OPEN 0u       /* WLAN_AUTH_OPEN（ieee80211.h:2481） */
#define AIC_JOIN_AUTH_SHARED_KEY 1u /* WLAN_AUTH_SHARED_KEY（:2482）WEP only */
#define AIC_JOIN_AUTH_FT 2u         /* WLAN_AUTH_FT（:2483） */
#define AIC_JOIN_AUTH_SAE 3u        /* WLAN_AUTH_SAE（:2484）WPA3 */
/* 本刀的 WPA2-PSK：802.11 认证层仍是 OPEN；名字写出来，dry 才看得出「不是忘了改」 */
#define AIC_JOIN_AUTH_WPA2_PSK AIC_JOIN_AUTH_OPEN
/*
 * Connection flags（lmac_mac.h:422-434）。WPA2-PSK + CCMP 时厂商会置这两位：
 *   CONTROL_PORT_HOST = BIT(0)：sme->crypto.control_port 为真 → EAPOL 归主机
 *                              （rwnx_msg_tx.c:2388-2389）
 *   WPA_WPA2_IN_USE   = BIT(3)：use_pairwise_key() 为真（非 WEP）→ WPA/WPA2
 *                              （rwnx_msg_tx.c:2394-2395 + :107-114）
 * 本刀开始收 EAPOL，所以两位都置；若真机关联反而退化，把 AIC_JOIN_FLAGS_WPA2
 * 改回 AIC_JOIN_FLAGS_NONE 是一次一行的回退（只影响固件行为，不上空口）。
 */
#define AIC_JOIN_FLAGS_NONE 0x00000000u
#define AIC_JOIN_FLAG_CONTROL_PORT_HOST 0x00000001u /* BIT(0) lmac_mac.h:424 */
#define AIC_JOIN_FLAG_WPA_WPA2_IN_USE 0x00000008u   /* BIT(3) lmac_mac.h:430 */
#define AIC_JOIN_FLAGS_WPA2 (AIC_JOIN_FLAG_CONTROL_PORT_HOST | AIC_JOIN_FLAG_WPA_WPA2_IN_USE)
/*
 * ctrl_port_ethertype = 0x888e（EAPOL，ETH_P_PAE）。
 * 厂商 rwnx_msg_tx.c:2403 直接把 cfg80211 的 control_port_ethertype 抄进来；WPA2 时
 * wpa_supplicant 给的就是 0x888e。本刀先把这个字段填对，等于提前声明"EAPOL 归主机"，
 * 但 flags 不置 CONTROL_PORT_HOST，所以固件这刀不会真的把 EAPOL 交给主机。
 */
#define AIC_JOIN_CTRL_PORT_EAPOL 0x888Eu
/* 厂商默认值：listen_itv=0（rwnx_mod_params.c:59）、listen_bcmc=true ⇒ dont_wait_bcmc=!true=0
   （rwnx_msg_tx.c:2426-2427）、uapsd_queues=IEEE80211_WMM_IE_STA_QOSINFO_AC_VO=1<<0
   （rwnx_mod_params.c:66 + include/linux/ieee80211.h:281）。 */
#define AIC_JOIN_LISTEN_INTERVAL 0u
#define AIC_JOIN_DONT_WAIT_BCMC 0u
#define AIC_JOIN_UAPSD_QUEUES 1u
/*
 * RSN IE：WPA2-PSK + CCMP，22 B（802.11-2016 §9.4.2.24 的元素结构）。
 * 字段出处：
 *   元素 id 48          WLAN_EID_RSN（linux_5.10/include/linux/ieee80211.h:2731）
 *   版本 1              RSN 元素版本号（§9.4.2.24.1）
 *   组播套件 00-0F-AC:4 WLAN_CIPHER_SUITE_CCMP（ieee80211.h:3386，SUITE 在 :3379）
 *   单播套件 00-0F-AC:4 同上（本刀只要 CCMP）
 *   AKM 00-0F-AC:2      WLAN_AKM_SUITE_PSK（ieee80211.h:3400）
 *   能力位 0x0000       不做 MFP、不预认证、不是 PMF 必需（§9.4.2.24.6）
 * 与 X64 的 IwlBuildStaRsn（Mac/Assoc/Rsn.c:130-151，Caps=0 时）逐字节一致。
 */
#define AIC_JOIN_RSN_IE_ID 48u          /* WLAN_EID_RSN */
#define AIC_JOIN_RSN_IE_BODY_LENGTH 20u /* 2+4+2+4+2+4+2 = 20，不含 id/len */
#define AIC_JOIN_RSN_IE_LENGTH 22u      /* sizeof 元素 = 2 + 20 */
#define AIC_JOIN_RSN_VERSION 1u
#define AIC_JOIN_RSN_SUITE_CCMP 4u  /* OUI 00-0F-AC:4 */
#define AIC_JOIN_RSN_SUITE_SUBCOUNT 1u
#define AIC_JOIN_RSN_AKM_PSK 2u /* OUI 00-0F-AC:2 */
#define AIC_JOIN_RSN_CAPABILITIES 0u
/* dry 没有卡：vif 用 0 展示布局（真跑时用扫描时 MM_ADD_IF 拿到的那个 vif） */
#define AIC_JOIN_VIF_INDEX_DRY 0u

/* ---- 收包预算 ----
 * CFM：4 × AIC_CFM_TIMEOUT_MS(500) = 2000 ms，与 ver/mmreset/bringup 同一档。
 * IND：6 × 500 = 3000 ms —— CFM 只是"受理"，IND 要等 auth+assoc 过完空口，
 *      厂商侧 open 关联通常几十 ms，给 3 s 是留重传余量（本刀自己定的预算，
 *      不是厂商常量；真机若 3 s 不够，先看 CFM 后有没有别的消息插队）。
 */
#define AIC_JOIN_CFM_TRIES 4u
#define AIC_JOIN_IND_TRIES 6u
/* 整帧上限 = AIC_MSG_MAX：IND 参数 852 B，远超 AicRecvMessageTimeout 的 512 B 契约，
   所以必须走 WifiFrame.c 的 AicReceiveFrameTimeout。 */
#define AIC_JOIN_FRAME_MAX 1600u

/*
 * IND 参数长度门槛：想读某字段，ParamLength 至少要覆盖到该字段末尾。
 * 20 = assoc_ie_buf 起点（status/bssid/roamed/三个 idx/qos/acm/两个 ie_len）。
 * 822 = aid 末尾；827 = width 末尾（center_freq 824..826、width 826..827）。
 * 数字都来自编译出的 offsetof+sizeof（见调研文档 §4），不手算。
 */
#define AIC_JOIN_IND_HEAD_END 20u
#define AIC_JOIN_IND_AID_END 822u
#define AIC_JOIN_IND_FREQ_END 827u

/*
 * struct sm_connect_req（lmac_msg.h:2215-2241），sizeof=320。
 * 厂商没用 __packed，按自然对齐；下面把编译器会插的填充全部显式写出：
 *   ssid@0(33) pad@33 bssid@34(6) chan@40(6) pad@46 flags@48
 *   ctrl_port_ethertype@52 ie_len@54 listen_interval@56 dont_wait_bcmc@58
 *   auth_type@59 uapsd_queues@60 vif_idx@61 pad@62 ie_buf@64(256)
 */
typedef struct {
    UINT8 SsidLength;                  /* +0   mac_ssid.length */
    UINT8 Ssid[AIC_JOIN_SSID_MAX];     /* +1   mac_ssid.array[32]，不保证结尾 0 */
    UINT8 SsidPad;                     /* +33  对齐 mac_addr（u16[3] 的 2 B 对齐） */
    UINT16 Bssid[3];                   /* +34  struct mac_addr（u16 array[3]） */
    AIC_SCAN_CHANNEL Chan;             /* +40  struct mac_chan_def：freq(2)+band(1)+flags(1)+tx_power(1)+pad(1) */
    UINT8 ChanPad[2];                  /* +46  对齐 u32 flags */
    UINT32 Flags;                      /* +48  mac_connection_flags */
    UINT16 CtrlPortEthertype;          /* +52  EAPOL = 0x888e */
    UINT16 IeLength;                   /* +54  关联请求附加 IE 的字节数 */
    UINT16 ListenInterval;             /* +56  */
    UINT8 DontWaitBcmc;                /* +58  bool_l（1 B） */
    UINT8 AuthType;                    /* +59  WLAN_AUTH_OPEN = 0 */
    UINT8 UapsdQueues;                 /* +60  bit0..3 = VO/VI/BE/BK */
    UINT8 VifIndex;                    /* +61  MM_ADD_IF_CFM 给的 inst_nbr */
    UINT8 IePad[2];                    /* +62  对齐 u32 ie_buf */
    UINT32 IeBuffer[64];               /* +64  u32_l ie_buf[64] = 256 B */
} AIC_SM_CONNECT;

/* struct sm_connect_cfm（lmac_msg.h:2244-2248），sizeof=1：0 = 已受理，随后会有 IND */
typedef struct {
    UINT8 Status; /* +0 */
} AIC_SM_CONNECT_CONFIRM;

/*
 * struct sm_connect_ind（lmac_msg.h:2253-2286），sizeof=852。
 * 只做指针转换，从不实例化；字段偏移全部由 AicJoinLayoutCheck 核对。
 *   status_code@0 bssid@2 roamed@8 vif_idx@9 ap_idx@10 ch_idx@11 qos@12 acm@13
 *   assoc_req_ie_len@14 assoc_rsp_ie_len@16 pad@18 assoc_ie_buf@20(800)
 *   aid@820 band@822 pad@823 center_freq@824 width@826 pad@827
 *   center_freq1@828 center_freq2@832 ac_param@836(16)
 */
typedef struct {
    UINT16 StatusCode;             /* +0   802.11 status code，0 = 关联成功 */
    UINT16 Bssid[3];               /* +2   实际连上的 BSSID */
    UINT8 Roamed;                  /* +8   1 = 漫游触发，不是本次主机请求 */
    UINT8 VifIndex;                /* +9    */
    UINT8 ApIndex;                 /* +10  固件给这个 AP 分配的 sta_idx（下一刀 MM_KEY_ADD 要用） */
    UINT8 ChIndex;                 /* +11  LMAC 信道上下文编号 */
    UINT8 Qos;                     /* +12  AP 支持 WMM */
    UINT8 Acm;                     /* +13  WMM ACM 位 */
    UINT16 AssocReqIeLength;       /* +14  */
    UINT16 AssocRspIeLength;       /* +16  */
    UINT16 IePad0;                 /* +18  对齐 u32 assoc_ie_buf */
    UINT32 AssocIeBuffer[200];     /* +20  SM_ASSOC_IE_LEN/4 = 800/4 */
    UINT16 Aid;                    /* +820  关联 ID（低 14 位有效） */
    UINT8 Band;                    /* +822  */
    UINT8 BandPad;                 /* +823  对齐 u16 center_freq */
    UINT16 CenterFrequency;        /* +824  MHz */
    UINT8 Width;                   /* +826  信道宽度枚举 */
    UINT8 WidthPad;                /* +827  对齐 u32 center_freq1 */
    UINT32 CenterFreq1;            /* +828  */
    UINT32 CenterFreq2;            /* +832  */
    UINT32 AcParam[4];             /* +836  AC_MAX = 4，每项一个 u32 EDCA 参数 */
} AIC_SM_CONNECT_INDICATION;

/* ---- 跨文件原型（WifiAssocMessage.c / WifiAssocReport.c / WifiAssoc.c） ---- */
/* Message：拼包 + 布局自检 */
UINT32 AicJoinBuild(UINT8 *Buffer, const AIC_SCAN_AP *Target, UINT8 VifIndex);
int AicJoinLayoutCheck(void);
/* 22 B WPA2-PSK/CCMP RSN IE（只读；AicJoinBuild 逐字节抄进 ie_buf） */
const UINT8 *AicJoinRsnIe(void);
/* Report：目标解析/打印、IND 解析/打印（只打印，不改状态） */
int AicJoinResolve(const char *Ssid, const char *BssidText, AIC_SCAN_AP *Out);
void AicJoinPlaceholder(const char *Ssid, AIC_SCAN_AP *Out);
void AicJoinPrintTarget(const AIC_SCAN_AP *Target);
void AicJoinPrintTx(UINT32 Length);
void AicJoinDump(const UINT8 *Data, UINT32 Length);
void AicJoinDumpRsn(const UINT8 *Request, UINT32 RequestLength);
/* 0 = IND status_code 为 0；-1 = 关联失败（原因已打印） */
int AicJoinHandleInd(const UINT8 *Param, UINT32 ParamLength);
/*
 * IND 成功后把两个索引留下来给下一段用（WifiControlPort.c 的 sta_idx、handshake 的 vif）：
 * ap_idx@10 = 固件分配的 sta_idx，vif_idx@9 = 关联所在 vif。**只在 status_code==0 时有效**，
 * 失败时不清零（调用方拿到失败就不会用）。只读写本文件的 static，不碰 SDIO。
 */
void AicJoinLastIndexes(UINT8 *VifIndex, UINT8 *ApIndex);
/* 板级入口：Dry != 0 只拼包（不需要卡）；BssidText 可为 0（用扫描缓存里最强的那个）；
   Password 可为 0（只到控制端口为止，不给就继续做四次握手） */
int SdhciWifiJoin(int Dry, const char *Ssid, const char *BssidText, const char *Password);
/*
 * 控制端口（WifiControlPort.c）：sizeof/offsetof 自检 → 拼 2 B → 发 0x1404。
 * Open = AIC_CONTROL_PORT_OPEN/CLOSE；StaIndex 用 IND.ap_idx。
 * **等 CFM 不在这里**：0x1405 与 M1 走同一条 SDIO FIFO，谁先谁后不确定，所以由
 * WifiHandshake.c 用同一条收包路径一起等（gAicEapolControlPortCfm）。
 */
int AicControlPortLayoutCheck(void);
UINT32 AicControlPortBuild(UINT8 *Buffer, UINT8 StaIndex, UINT8 Open);
int AicControlPortSend(SDHCI_HOST *Host, UINT8 StaIndex);
/*
 * 关联成功之后的固定四步：控制端口 → M1 → PTK/M2 → M3（WifiControlPort.c /
 * WifiHandshake*.c / WifiEapolTx.c）。**本函数不关联**，只接在 join 后面。
 * Password 为 0 时只做到控制端口为止（用于"没有密码也想看 M1 到底来不来"）。
 */
int AicWifiHandshakeRun(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                        UINT8 VifIndex, UINT8 ApIndex);

#endif
