/*
 * WifiHandshakeRun.c — 控制口时机 + join 后整段编排（从 WifiHandshake.c 拆出）
 *
 * 默认 portearly=0：不对早发 ME_SET_CONTROL_PORT；装钥后 AUTHORIZED（Install）。
 * portearly=1：旧早开 + 等 CFM，同时收可能先到的 M1。
 */
#include "WifiHandshakePrivate.h"
#include "WifiAssocPrivate.h"
#include "CString.h"
#include "WifiNetif.h"  /* Arm/Disarm 网桥（dwifi-netif-3） */
#include "Hal.h"

static int HandshakeControlPortDone(void) {
    return (int)gAicEapolControlPortCfm;
}

static int HandshakeOpenControlPort(SDHCI_HOST *Host, UINT8 VifIndex, UINT8 ApIndex) {
    AIC_EAPOL_COUNTERS Counters;

    AicHandshakeArmForM1();
    if (gAicControlPortEarlyOpen == 0u) {
        SdhciMark("WiFi: control port deferred (vendor AUTHORIZED after keys; portearly=0 vif=");
        SdhciMarkDecimal("", VifIndex);
        SdhciMark(" ap=");
        SdhciMarkDecimal("", ApIndex);
        SdhciMark(")\n");
        return 0;
    }
    if (AicControlPortSend(Host, ApIndex) != 0) {
        return -1;
    }
    gAicEapolControlPortCfm = 0u;
    memset(&Counters, 0, sizeof(Counters));
    if (AicEapolPollRun(Host, AIC_HANDSHAKE_CONTROL_PORT_MS, &Counters,
                        HandshakeControlPortDone) != 0) {
        SdhciMark("WiFi: handshake FAILED at control port: SDIO read failed\n");
        return -1;
    }
    if (gAicEapolControlPortCfm == 0u) {
        SdhciMark("WiFi: handshake FAILED at control port: no ME_SET_CONTROL_PORT_CFM(0x1405)"
                  " in ");
        SdhciMarkDecimal("", AIC_HANDSHAKE_CONTROL_PORT_MS);
        SdhciMark("ms (firmware did not accept the control port request;"
                  " check sta_idx/ap_idx and the join flags)\n");
        return -1;
    }
    SdhciMark("WiFi: control port open (vif=");
    SdhciMarkDecimal("", VifIndex);
    SdhciMark(" ap=");
    SdhciMarkDecimal("", ApIndex);
    SdhciMark(")\n");
    return 0;
}

static int HandshakeRunKeyed(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                             UINT8 VifIndex, UINT8 ApIndex) {
    if (AicHandshakeReceiveM1(Host) != 0) {
        return -1;
    }
    if (AicHandshakeSendM2(Host, Target, Password, VifIndex, ApIndex) != 0) {
        return -1;
    }
    if (AicHandshakeWaitM3(Host) != 0) {
        return -1;
    }
    /*
     * M3 的 MIC 已过：摘掉 sink。装钥匙期间 AP 可能重传 M3（它还没收到 M4），
     * 那些重传照旧由 WifiEapolKey.c 打印，但不该再改写上面留下的 M3 状态。
     */
    AicEapolSetKeySink(0);
    if (AicHandshakeInstallKeys(Host, Target, VifIndex, ApIndex) != 0) {
        return -1;
    }
    SdhciMark("WiFi: eapol handshake ok (keys installed: ptk+gtk)\n");
    /*
     * dwifi-netif-3：装钥成功 ⇒ 武装通用网（TX 走 AIC、RX 喂 lwIP、对外 MAC = 站点 MAC），
     * 并确保 lwIP 起栈。未武装前（join 中/失败）一律保持有线路径不受影响。
     */
    if (BoardWifiArmNetifFromJoin() == 0) {
        HalNetEnsureLwIpActive();
    }
    AicHandshakeWatchData(Host);
    return 0;
}

int AicWifiHandshakeRun(SDHCI_HOST *Host, const AIC_SCAN_AP *Target, const char *Password,
                        UINT8 VifIndex, UINT8 ApIndex) {
    int Status;

    if (AicEapolLayoutCheck() != 0) {
        return -1;
    }
    /* 新一次 join 前先解除旧武装，避免用旧 MAC/旧索引喂新连接 */
    BoardWifiDisarmNetifLinkOutput();
    gAicEapolKeysInstalled = 0u;
    if (HandshakeOpenControlPort(Host, VifIndex, ApIndex) != 0) {
        AicHandshakeSessionStop();
        return -1;
    }
    if (Password == 0 || Password[0] == 0) {
        AicHandshakeSessionStop();
        SdhciMark("WiFi: join: no password, stop at control port"
                  " (add the password to run the 4-way handshake)\n");
        return 0;
    }
    Status = HandshakeRunKeyed(Host, Target, Password, VifIndex, ApIndex);
    AicHandshakeSessionStop();
    return Status;
}
