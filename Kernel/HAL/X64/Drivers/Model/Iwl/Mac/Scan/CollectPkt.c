/*
 * CollectPkt.c — 扫描收包单帧（PR-NORM-12：诊断日志外搬）
 */
#include "Private.h"
#include "HalSerial.h"

void IwlScanCollectContextInitialize(IWL_SCAN_COLLECT_CTX *C) {
    C->Ncode = 0;
    C->GotAck = 1;
    C->GotDone = 0;
    C->StopAt = -1;
    C->BeaconN = 0;
    C->FirstFc = 0;
    C->Heard[0] = 0;
}

/* 1 = 命中目标 SSID（调用方应 return 1）；0 = 继续 */
int IwlScanCollectOnPkt(IWL_SCAN_COLLECT_CTX *C, IWL_RX_PKT *Pkt, UINTN Len,
                        UINT32 LoopI) {
    const UINT8 *Raw = (const UINT8 *)&Pkt->Hdr;
    UINT8 Code = Pkt->Hdr.Code;
    UINT8 Flags = Pkt->Hdr.Flags;
    UINT8 QueueIdentifier = Pkt->Hdr.Qid;
    int IsNotification = (QueueIdentifier & 0x80u) != 0;
    UINTN HeaderSize = (Len >= sizeof(IWL_CMD_HDR_WIDE) && Flags &&
                        Len != sizeof(IWL_CMD_HDR))
                           ? sizeof(IWL_CMD_HDR_WIDE)
                           : sizeof(IWL_CMD_HDR);
    const UINT8 *Payload = Raw + HeaderSize;
    UINTN PayloadLength = Len > HeaderSize ? Len - HeaderSize : 0;

    gIwlScanCount++;
    if (C->Ncode < 8) {
        C->Codes[C->Ncode++] = Code | (IsNotification ? 0x100u : 0);
    }
    IwlScanCollectLogEarlyPacket(Pkt, Len, IsNotification);
    if (!IsNotification && Code == IWL_CMD_SCAN_REQ_UMAC) {
        C->GotAck = 1;
        IwlScanCollectLogScanAcknowledge(Raw, Len);
    }
    if (Code == 0x02u && PayloadLength >= 12) {
        IwlScanCollectLogScanError(Payload, PayloadLength);
    }
    if (Code == 0xc0u && PayloadLength >= 24) {
        /* RX_PHY：channel 在 phy_info+22（le16） */
        gLastPhyChan = Payload[22];
    }
    /*
     * 刀 #81：#80 ack+C1+done 但 fail。8000 RX_MPDU =
     * iwl_rx_mpdu_res_start(4) + 802.11；旧试 +0/+16 漏 +4。
     * rxraw: …8101000C8000… → +4 起 FC=0x80 beacon。
     */
    if (Code == IWL_RX_MPDU_CMD || Code == 0xc3 || Code == IWL_BEACON_NOTIFICATION) {
        const UINT8 *Frame = Payload;
        UINTN FrameLength = PayloadLength;
        if (Code == IWL_RX_MPDU_CMD && PayloadLength > 4) {
            Frame = Payload + 4;
            FrameLength = PayloadLength - 4;
        }
        if (Code == IWL_RX_MPDU_CMD && FrameLength > 0 && C->FirstFc == 0) {
            C->FirstFc = Frame[0];
        }
        if (FrameLength > 32 &&
            ((Frame[0] & 0xFCu) == 0x80u || (Frame[0] & 0xFCu) == 0x50u)) {
            C->BeaconN++;
            IwlNoteBeacon(Frame, FrameLength, C->Heard);
        }
        if (FrameLength > 32 && IwlParseBeacon(Frame, FrameLength)) {
            IwlLogStage("scan=ok");
            return 1;
        }
    }
    if (Code == IWL_SCAN_START_UMAC) {
        IwlLogVerb("scan=start");
    } else if (Code == IWL_SCAN_COMPLETE_UMAC || Code == IWL_SCAN_COMPLETE_LMAC ||
               Code == IWL_SCAN_ITERATION_COMPLETE_UMAC) {
        C->GotDone = 1;
        if (gIwlSsidOk) {
            IwlLogStage("scan=ok");
            return 1;
        }
        /* 完成通知后面可能还有信标，先把本轮队列收完 */
        if (C->StopAt < 0) {
            C->StopAt = (int)LoopI + 400;
        }
    }
    return 0;
}
