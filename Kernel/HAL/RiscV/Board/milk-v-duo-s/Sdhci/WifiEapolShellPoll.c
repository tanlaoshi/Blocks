/*
 * WifiEapolShellPoll.c — `wififw eapol poll [ms]` 入口（从 WifiEapol.c 拆出）
 */
#include "WifiEapolPrivate.h"
#include "CString.h"

static int EapolParseMilliseconds(const char *Text, UINT32 *OutMilliseconds) {
    UINT32 Value = 0;
    UINT32 Index = 0;

    if (Text == 0 || Text[0] == 0) {
        return -1;
    }
    while (Text[Index] != 0) {
        char One = Text[Index];

        if (One < '0' || One > '9') {
            return -1;
        }
        Value = Value * 10u + (UINT32)(One - '0');
        if (Value > AIC_EAPOL_POLL_MAX_MS) {
            return -1;
        }
        Index++;
    }
    if (Value == 0u) {
        return -1;
    }
    *OutMilliseconds = Value;
    return 0;
}

int SdhciWifiEapolPoll(const char *MsText) {
    SDHCI_HOST *Host;
    AIC_EAPOL_COUNTERS Counters;
    UINT32 Budget = AIC_EAPOL_POLL_DEFAULT_MS;
    int Status;

    if (MsText != 0 && EapolParseMilliseconds(MsText, &Budget) != 0) {
        SdhciMark("WiFi: eapol poll: bad ms (decimal 1..60000)\n");
        return -1;
    }
    Host = SdhciHostAt(1);
    if (Host == 0 || !SdhciWifiReady()) {
        SdhciMark("WiFi: eapol poll needs a probed card (run wifiprobe + wififw join first)\n");
        return -1;
    }
    if (AicEapolLayoutCheck() != 0) {
        return -1;
    }
    if (AicWifiVendorPrepare(Host) != 0) {
        SdhciMark("WiFi: eapol poll: vendor prepare failed\n");
        return -1;
    }
    memset(&Counters, 0, sizeof(Counters));
    SdhciMark("WiFi: eapol poll: budget=");
    SdhciMarkDecimal("", Budget);
    SdhciMark("ms, data frames only (type&0x10==0), looking for EAPOL/0x888e\n");
    Status = AicEapolPollRun(Host, Budget, &Counters, 0);
    SdhciMark("WiFi: eapol poll: done frames=");
    SdhciMarkDecimal("", Counters.Frames);
    SdhciMark(" data=");
    SdhciMarkDecimal("", Counters.DataFrames);
    SdhciMark(" config=");
    SdhciMarkDecimal("", Counters.ConfigFrames);
    SdhciMark(" eapol=");
    SdhciMarkDecimal("", Counters.EapolFrames);
    SdhciMark(" key=");
    SdhciMarkDecimal("", Counters.KeyFrames);
    SdhciMark(" polls=");
    SdhciMarkDecimal("", Counters.Polls);
    SdhciMark(" empty=");
    SdhciMarkDecimal("", Counters.EmptyPolls);
    SdhciMark(" bad=");
    SdhciMarkDecimal("", Counters.BadFrames);
    SdhciMark("\n");
    if (Status != 0) {
        SdhciMark("WiFi: eapol poll: SDIO read failed (FIFO may be out of sync; re-run join)\n");
        return -1;
    }
    if (Counters.KeyFrames == 0u) {
        SdhciMark("WiFi: eapol poll: no EAPOL-Key seen (joined WPA2 AP? AP still at assoc?)\n");
        return -1;
    }
    return 0;
}
