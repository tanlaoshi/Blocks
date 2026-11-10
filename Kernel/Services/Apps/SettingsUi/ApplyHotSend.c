/*
 * ApplyHotSend.c — 设置「热传」：听/停、端口、复位/备份默认
 */
#include "SettingsUiPrivate.h"
#include "Hal/Ksend.h"
#include "Db.h"

static const UINT16 gHotSendPorts[HOTSEND_PORT_COUNT] = {
    26992u, 26993u, 27000u
};

UINT16 HotSendPortAt(int Index) {
    if (Index < 0 || Index >= HOTSEND_PORT_COUNT) {
        return gHotSendPorts[0];
    }
    return gHotSendPorts[Index];
}

int HotSendItemMarked(int Index) {
    UINT16 Port;
    int Reboot;
    int Backup;
    int PortIndex;

    if (Index <= 0 || Index >= HOTSEND_ITEM_COUNT) {
        return 0;
    }
    Port = KsendServePreferencePort();
    Reboot = KsendServePreferenceReboot() ? 1 : 0;
    Backup = KsendServePreferenceBackup() ? 1 : 0;
    if (Index >= 1 && Index <= HOTSEND_PORT_COUNT) {
        PortIndex = Index - 1;
        return HotSendPortAt(PortIndex) == Port ? 1 : 0;
    }
    if (Index == 4) {
        return Reboot ? 1 : 0;
    }
    if (Index == 5) {
        return Reboot ? 0 : 1;
    }
    if (Index == 6) {
        return Backup ? 1 : 0;
    }
    if (Index == 7) {
        return Backup ? 0 : 1;
    }
    return 0;
}

void ApplyHotSendChoice(int Index) {
    UINT16 Port;

    if (Index < 0 || Index >= HOTSEND_ITEM_COUNT) {
        return;
    }
    (void)DbInitialize();
    if (Index == 0) {
        if (KsendServeState() == KSEND_SERVE_LISTENING) {
            KsendServeListenStop();
        } else if (KsendServeState() == KSEND_SERVE_TRANSFERRING) {
            KsendServeRequestAbort();
        } else {
            Port = KsendServePreferencePort();
            (void)KsendServeListenStart(Port, 0u);
        }
        return;
    }
    if (Index >= 1 && Index <= HOTSEND_PORT_COUNT) {
        if (KsendServeState() != KSEND_SERVE_IDLE) {
            /* 听中改端口：先停再记下，需再点开始听 */
            KsendServeListenStop();
        }
        KsendServePreferenceSetPort(HotSendPortAt(Index - 1));
        return;
    }
    if (Index == 4) {
        KsendServePreferenceSetReboot(1);
        return;
    }
    if (Index == 5) {
        KsendServePreferenceSetReboot(0);
        return;
    }
    if (Index == 6) {
        KsendServePreferenceSetBackup(1);
        return;
    }
    if (Index == 7) {
        KsendServePreferenceSetBackup(0);
    }
}
