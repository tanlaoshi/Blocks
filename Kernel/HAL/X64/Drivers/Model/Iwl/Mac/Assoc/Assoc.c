/*
 * Assoc.c — 关联入口编排（PR-NORM-12）
 */
#include "Private.h"
#include "HalSerial.h"

UINT8 gIwlStaRsn[32];
UINT8 gIwlStaRsnLen;

int IwlAssocRun(void) {
    UINT8 Frame[160];
    UINT8 Payload[120];
    UINTN FrameLength = 0;
    UINTN PayloadLength = 0;
    UINTN Index;
    IWL_RX_PKT *Packet;
    UINTN Length;
    int GotAssociation = 0;
    int Attempt;
    UINT32 ReceiveCount = 0;
    UINT8 FirstFrameControl = 0;

    if (!gIwlSsidOk) {
        return 0;
    }
    IwlAssocCopyN(gIwlBssid, gIwlTarget.Bssid, 6);
    gIwlAssociated = 0;
    gIwlAid = 0;
    gIwlStaRsnLen = 0;

    (void)IwlPhyContextTune(gIwlTarget.Chan);

    {
        int EverAuthenticated = 0;

        for (Attempt = 0; Attempt < 4 && !GotAssociation; Attempt++) {
            ReceiveCount = 0;
            FirstFrameControl = 0;

            if (!EverAuthenticated) {
                Payload[0] = 0;
                Payload[1] = 0;
                Payload[2] = 1;
                Payload[3] = 0;
                Payload[4] = 0;
                Payload[5] = 0;
                IwlBuildMgmt(Frame, &FrameLength, 0xB, Payload, 6);
                if (IwlSendFrameRaw(Frame, FrameLength) != 0) {
                    return 0;
                }
                if (Attempt == 0) {
                    (void)IwlSendFrameRaw(Frame, FrameLength);
                }
                if (!IwlAssocWaitAuth(&ReceiveCount, &FirstFrameControl)) {
                    continue;
                }
                IwlLogStage("auth=ok");
                EverAuthenticated = 1;
            } else {
                IwlLogVerb("auth=keep");
            }

            if (!IwlAssocBuildRequestPayload(Payload, sizeof(Payload), &PayloadLength,
                                             Attempt)) {
                return 0;
            }
            IwlBuildMgmt(Frame, &FrameLength, 0x0, Payload, PayloadLength);
            if (Attempt == 0) {
                IwlAssocLogInformationElements();
            }
            if (IwlSendFrameRaw(Frame, FrameLength) != 0) {
                IwlLogStage("assoc=txfail");
                return 0;
            }
            IwlLogStage("assoc=tx");
            if (Attempt == 0) {
                (void)IwlSendFrameRaw(Frame, FrameLength);
            }
            IwlLogStage("assoc=wait");
            ReceiveCount = 0;
            FirstFrameControl = 0;
            if (!IwlAssocWaitResp(&ReceiveCount, &FirstFrameControl, &GotAssociation)) {
                if (Attempt < 3) {
                    IwlLogStage("assoc=retry");
                }
                continue;
            }
        }
    }
    if (!GotAssociation) {
        IwlLogStage("assoc=fail");
        return 0;
    }
    gIwlAssociated = 1;
    IwlLogStage("assoc=ok");
    if (!IwlMacCtxtPrep()) {
        IwlLogStage("prep=soft");
    }
    IwlLogStage("drain=go");
    for (Index = 0; Index < 80; Index++) {
        int Took = 0;

        IwlReceivePoll();
        while (Took < 32 && IwlAssocTake(&Packet, &Length)) {
            const UINT8 *DotEleven;
            UINTN PayloadBytes;

            Took++;
            if (Packet->Hdr.Code != IWL_RX_MPDU_CMD || Length < 12) {
                continue;
            }
            DotEleven = Packet->Data + 4;
            PayloadBytes = Length - sizeof(IWL_CMD_HDR) - 4;
            if (PayloadBytes >= 24 && ((DotEleven[0] >> 2) & 0x3u) == 0x2u) {
                IwlReceiveHoldMpdu(Packet, Length);
            }
        }
        IwlStallMilliseconds(1);
    }
    return 1;
}
