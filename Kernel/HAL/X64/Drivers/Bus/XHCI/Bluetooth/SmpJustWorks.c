/*
 * SmpJustWorks.c — LE SMP Just Works（NoInputNoOutput）；Central 发起。
 * 失败阶段写入 LeLastError（op=阶段 status=原因/FE）。
 */
#include "XHCI/Bluetooth/Private.h"
#include "XHCI.h"
#define L2CAP_CID_SMP 0x0006u
#define SMP_PAIRING_REQUEST 0x01u
#define SMP_PAIRING_RESPONSE 0x02u
#define SMP_PAIRING_CONFIRM 0x03u
#define SMP_PAIRING_RANDOM 0x04u
#define SMP_PAIRING_FAILED 0x05u
#define SMP_SECURITY_REQUEST 0x0Bu
#define SMP_IO_NO_INPUT_NO_OUTPUT 0x03u
#define SMP_AUTH_BONDING 0x01u
#define SMP_AUTH_SC 0x08u
#define SMP_SLICE_MS 50u
#define SMP_DEFAULT_MS 20000u
#define SMP_STAGE_SEND_REQUEST 0x0001u
#define SMP_STAGE_WAIT_RESPONSE 0x0002u
#define SMP_STAGE_WAIT_CONFIRM 0x0003u
#define SMP_STAGE_WAIT_RANDOM 0x0004u
#define SMP_STAGE_VERIFY 0x0005u
#define SMP_STAGE_ENCRYPT 0x0006u
#define SMP_STAGE_SC_UNSUPPORTED 0x0007u
static void SmpPump(void) {
    if (HalCpuIsHypervisor()) {
        ProcessEvents();
    } else {
        ProcessEventsRealPc();
    }
    BluetoothHostPump();
    ServiceHidCompletions();
}
static int SmpSend(UINT16 Handle, const UINT8 *Smp, UINT16 SmpLength) {
    UINT8 Frame[32];
    UINT16 Index;
    if (Smp == 0 || SmpLength == 0u || SmpLength + 4u > sizeof(Frame)) {
        return -1;
    }
    Frame[0] = (UINT8)(SmpLength & 0xFFu);
    Frame[1] = (UINT8)((SmpLength >> 8) & 0xFFu);
    Frame[2] = (UINT8)(L2CAP_CID_SMP & 0xFFu);
    Frame[3] = (UINT8)((L2CAP_CID_SMP >> 8) & 0xFFu);
    for (Index = 0; Index < SmpLength; Index++) {
        Frame[4u + Index] = Smp[Index];
    }
    return BluetoothAclOutSend(Handle, Frame, (UINT16)(4u + SmpLength));
}
static int SmpTakeAny(UINT8 *SmpOut, UINT16 Cap, UINT16 *SmpLengthOut,
                      UINT32 TimeoutMs) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT8 Acl[64];
        UINT16 AclLength = 0;
        UINT16 L2Length;
        UINT16 Cid;
        UINT16 Index;
        SmpPump();
        while (BluetoothAclInboundTake(Acl, sizeof(Acl), &AclLength)) {
            if (AclLength < 5u) {
                continue;
            }
            L2Length = (UINT16)(Acl[0] | ((UINT16)Acl[1] << 8));
            Cid = (UINT16)(Acl[2] | ((UINT16)Acl[3] << 8));
            if (Cid != L2CAP_CID_SMP || L2Length == 0u ||
                (UINT16)(4u + L2Length) > AclLength) {
                continue;
            }
            if (L2Length > Cap) {
                L2Length = Cap;
            }
            for (Index = 0; Index < L2Length; Index++) {
                SmpOut[Index] = Acl[4u + Index];
            }
            if (SmpLengthOut != 0) {
                *SmpLengthOut = L2Length;
            }
            return 0;
        }
        StallMilliseconds(SMP_SLICE_MS);
    }
    return -1;
}
static int SmpExpect(UINT8 WantCode, UINT8 *SmpOut, UINT16 Cap,
                     UINT16 *SmpLengthOut, UINT32 TimeoutMs, UINT16 Stage) {
    UINT64 Start = ReadTsc();
    UINT64 Need = (UINT64)TimeoutMs * 3000000ULL;
    while (ReadTsc() - Start < Need) {
        UINT16 Left;
        UINT16 GotLength = 0;
        UINT64 Elapsed = ReadTsc() - Start;
        if (Elapsed >= Need) {
            break;
        }
        Left = (UINT16)((Need - Elapsed) / 3000000ULL);
        if (Left < 50u) {
            Left = 50u;
        }
        if (SmpTakeAny(SmpOut, Cap, &GotLength, Left) < 0) {
            continue;
        }
        if (SmpOut[0] == SMP_PAIRING_FAILED) {
            UINT8 Reason = GotLength > 1u ? SmpOut[1] : 0xFFu;
            BluetoothLeLastErrorSet(Stage, Reason);
            BootLogHex("bt: smp Pairing Failed reason=", Reason, 2);
            return -1;
        }
        if (SmpOut[0] == SMP_SECURITY_REQUEST) {
            continue;
        }
        if (SmpOut[0] == WantCode) {
            if (SmpLengthOut != 0) {
                *SmpLengthOut = GotLength;
            }
            return 0;
        }
        BootLogHex("bt: smp skip code=", SmpOut[0], 2);
    }
    BluetoothLeLastErrorSet(Stage, 0xFEu);
    return -1;
}
static void BuildPairingRequest(UINT8 Out[7]) {
    Out[0] = SMP_PAIRING_REQUEST;
    Out[1] = SMP_IO_NO_INPUT_NO_OUTPUT;
    Out[2] = 0x00u;
    Out[3] = SMP_AUTH_BONDING; /* 无 MITM / 无 SC → legacy Just Works */
    Out[4] = 16u;
    Out[5] = 0x00u;
    Out[6] = 0x00u;
}
int XhciBluetoothSmpJustWorks(UINT32 TimeoutMs) {
    UINT16 Handle = 0;
    UINT8 Peer[6];
    UINT8 PeerType = 0;
    UINT8 Local[6];
    UINT8 Preq[7];
    UINT8 Pres[7];
    UINT8 Mrand[16];
    UINT8 Srand[16];
    UINT8 Mconfirm[16];
    UINT8 Sconfirm[16];
    UINT8 Check[16];
    UINT8 Tk[16];
    UINT8 Stk[16];
    UINT8 Smp[24];
    UINT16 SmpLength = 0;
    UINT32 Index;
    if (TimeoutMs == 0u) {
        TimeoutMs = SMP_DEFAULT_MS;
    }
    BluetoothLeLastErrorSet(0, 0);
    if (BluetoothLeConnectionHandleGet(&Handle) < 0 ||
        BluetoothLePeerAddressGet(Peer, &PeerType) < 0) {
        BootLog("bt: smp need le connect\n");
        BluetoothLeLastErrorSet(SMP_STAGE_SEND_REQUEST, 0xFDu);
        return -1;
    }
    if (!BluetoothHostPumpIsActive()) {
        return -1;
    }
    for (Index = 0; Index < 6u; Index++) {
        Local[Index] = gBluetoothAddress[Index];
    }
    for (Index = 0; Index < 16u; Index++) {
        Tk[Index] = 0;
    }
    BluetoothHostEventQueueFlush();
    BluetoothAclFlowReset();
    if (BluetoothAclFlowInitialize() < 0) {
        BootLog("bt: smp acl flow fail\n");
        BluetoothLeLastErrorSet(SMP_STAGE_SEND_REQUEST, 0xFCu);
        return -1;
    }
    BuildPairingRequest(Preq);
    /* 不 Flush 入队：连后可能已有 Security Request */
    (void)SmpTakeAny(Smp, sizeof(Smp), &SmpLength, 300u);
    if (SmpSend(Handle, Preq, 7) < 0) {
        BootLog("bt: smp Pairing Request send fail\n");
        BluetoothLeLastErrorSet(SMP_STAGE_SEND_REQUEST, 0xFEu);
        return -1;
    }
    if (SmpExpect(SMP_PAIRING_RESPONSE, Smp, sizeof(Smp), &SmpLength,
                  TimeoutMs, SMP_STAGE_WAIT_RESPONSE) < 0 ||
        SmpLength < 7u) {
        BootLog("bt: smp wait Pairing Response fail\n");
        return -1;
    }
    for (Index = 0; Index < 7u; Index++) {
        Pres[Index] = Smp[Index];
    }
    if ((Pres[3] & SMP_AUTH_SC) != 0u && (Preq[3] & SMP_AUTH_SC) == 0u) {
        /* 对端单方面要 SC：本刀只做 legacy JW */
        BootLog("bt: smp peer wants SC (unsupported)\n");
        BluetoothLeLastErrorSet(SMP_STAGE_SC_UNSUPPORTED, Pres[3]);
        return -1;
    }
    BluetoothSmpFillRandom(Mrand);
    if (BluetoothSmpC1(Tk, Mrand, Preq, Pres, 0, Local, PeerType, Peer,
                       Mconfirm) < 0) {
        return -1;
    }
    Smp[0] = SMP_PAIRING_CONFIRM;
    for (Index = 0; Index < 16u; Index++) {
        Smp[1u + Index] = Mconfirm[Index];
    }
    if (SmpSend(Handle, Smp, 17) < 0) {
        BluetoothLeLastErrorSet(SMP_STAGE_WAIT_CONFIRM, 0xFEu);
        return -1;
    }
    if (SmpExpect(SMP_PAIRING_CONFIRM, Smp, sizeof(Smp), &SmpLength, TimeoutMs,
                  SMP_STAGE_WAIT_CONFIRM) < 0 ||
        SmpLength < 17u) {
        BootLog("bt: smp wait Confirm fail\n");
        return -1;
    }
    for (Index = 0; Index < 16u; Index++) {
        Sconfirm[Index] = Smp[1u + Index];
    }
    Smp[0] = SMP_PAIRING_RANDOM;
    for (Index = 0; Index < 16u; Index++) {
        Smp[1u + Index] = Mrand[Index];
    }
    if (SmpSend(Handle, Smp, 17) < 0) {
        BluetoothLeLastErrorSet(SMP_STAGE_WAIT_RANDOM, 0xFEu);
        return -1;
    }
    if (SmpExpect(SMP_PAIRING_RANDOM, Smp, sizeof(Smp), &SmpLength, TimeoutMs,
                  SMP_STAGE_WAIT_RANDOM) < 0 ||
        SmpLength < 17u) {
        BootLog("bt: smp wait Random fail\n");
        return -1;
    }
    for (Index = 0; Index < 16u; Index++) {
        Srand[Index] = Smp[1u + Index];
    }
    if (BluetoothSmpC1(Tk, Srand, Preq, Pres, 0, Local, PeerType, Peer,
                       Check) < 0) {
        return -1;
    }
    for (Index = 0; Index < 16u; Index++) {
        if (Check[Index] != Sconfirm[Index]) {
            BootLog("bt: smp Confirm verify fail\n");
            BluetoothLeLastErrorSet(SMP_STAGE_VERIFY, 0xFBu);
            return -1;
        }
    }
    if (BluetoothSmpS1(Tk, Srand, Mrand, Stk) < 0) {
        return -1;
    }
    if (BluetoothSmpStartEncryption(Handle, Stk) < 0) {
        BluetoothLeLastErrorSet(SMP_STAGE_ENCRYPT, 0xFEu);
        return -1;
    }
    BootLog("bt: smp ok\n");
    BluetoothRequeueHid();
    return 0;
}
