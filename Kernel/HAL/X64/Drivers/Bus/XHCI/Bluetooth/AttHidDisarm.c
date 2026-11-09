/*
 * AttHidDisarm.c — 卸 CCCD（试错下一路时）
 *
 * 环被打坏的原因：旧 BluetoothRequeueHid 热路径无条件 QueueMouseIntr（双投递）。
 * 已禁该行为；成功路径勿再 SyncIntrDequeue——那是事后补锅，不是正常态。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
void BluetoothAttHidRecoverWiredHid(void) {
    UINT32 Round;
    if (gMouseIntrHold) {
        MouseHoldUntilReveal(0);
    }
    for (Round = 0; Round < 4u; Round++) {
        if (HalCpuIsHypervisor()) {
            ProcessEvents();
        } else {
            ProcessEventsRealPc();
        }
        ServiceHidCompletions();
        StallMilliseconds(5u);
    }
}
void BluetoothAttHidDisarmClientConfiguration(UINT16 ConnectionHandle,
                                             const UINT16 *ConfigurationHandles,
                                             UINT16 Count) {
    UINT16 Index;
    UINT16 Retry;
    UINT8 Disable[2];
    Disable[0] = 0x00u;
    Disable[1] = 0x00u;
    BluetoothAttNotifyAcceptSet(0);
    if (ConfigurationHandles == 0 || Count == 0u) {
        return;
    }
    for (Index = 0; Index < Count; Index++) {
        UINT16 ConfigurationHandle = ConfigurationHandles[Index];
        if (ConfigurationHandle == 0u) {
            continue;
        }
        for (Retry = 0; Retry < 3u; Retry++) {
            (void)BluetoothAttWriteCommand(ConnectionHandle, ConfigurationHandle,
                                           Disable, 2);
            ServiceHidCompletions();
            StallMilliseconds(5u);
            BluetoothHostPump();
        }
        BootLogHex("bt: hid notify off cccd=", ConfigurationHandle, 4);
    }
}
