/*
 * WifiJoinProbe.c — join 前读 BLOCKS:probe.cfg（从 WifiAssoc.c 拆出）
 *
 *   m4install=0|1   M4 Key Information install 位
 *   gtkmode=<n>     gAicGtkProbeMode（0=厂商默认钥；6=旧实验 pairwise 盖 PTK 槽）
 *   portearly=0|1   gAicControlPortEarlyOpen（0=装钥后 AUTHORIZED；1=旧早开）
 *   nogtk=1         gAicSkipGroupKeyInstall（跳过 GTK MM_KEY_ADD）
 *   noauthz=1       gAicSkipAuthorizedAfterKeys（跳过装钥后 AUTHORIZED）
 *   gtkfirst=1      gAicGroupKeyBeforePairwise（先 GTK 再 PTK；Docs/21 §1a）
 *   noptk=1         gAicSkipPairwiseKeyInstall（跳过 PTK；Docs/21 §1c）
 *   gtkreadd=1      gAicGroupKeyAddAgainAfterBoth（两钥后再装一次 GTK；Docs/21 §1d）
 */
#include "WifiAssocPrivate.h"
#include "WifiHandshakePrivate.h"
#include "FileSystem.h"

extern UINT32 gAicM4InstallBit;
extern UINT32 gAicGtkProbeMode;
extern UINT32 gAicControlPortEarlyOpen;
extern UINT32 gAicSkipGroupKeyInstall;
extern UINT32 gAicSkipAuthorizedAfterKeys;
extern UINT32 gAicGroupKeyBeforePairwise;
extern UINT32 gAicSkipPairwiseKeyInstall;
extern UINT32 gAicGroupKeyAddAgainAfterBoth;

void AicWifiJoinLoadProbe(void) {
    char Text[144];
    UINTN Got = 0;
    UINTN Offset;

    /* 缺键 = 默认：避免上一轮 probe 残留在 BSS 全局里 */
    gAicGtkProbeMode = 0u;
    gAicControlPortEarlyOpen = 0u;
    gAicSkipGroupKeyInstall = 0u;
    gAicSkipAuthorizedAfterKeys = 0u;
    gAicGroupKeyBeforePairwise = 0u;
    gAicSkipPairwiseKeyInstall = 0u;
    gAicGroupKeyAddAgainAfterBoth = 0u;
    if (FileSystemReadFileAt("BLOCKS:probe.cfg", 0, Text, sizeof(Text) - 1u, &Got) != 0 ||
        Got == 0u) {
        return;
    }
    Text[Got] = 0;
    for (Offset = 0; Offset < Got; Offset++) {
        if (Offset + 10u < Got && Text[Offset] == 'm' && Text[Offset + 1u] == '4' &&
            Text[Offset + 2u] == 'i' && Text[Offset + 3u] == 'n' && Text[Offset + 4u] == 's' &&
            Text[Offset + 5u] == 't' && Text[Offset + 6u] == 'a' && Text[Offset + 7u] == 'l' &&
            Text[Offset + 8u] == 'l' && Text[Offset + 9u] == '=') {
            gAicM4InstallBit = (Text[Offset + 10u] == '0') ? 0u : 1u;
            SdhciMark("WiFi: probe: m4 install bit=");
            SdhciMarkDecimal("", gAicM4InstallBit);
            SdhciMark("\n");
        }
        if (Offset + 8u < Got && Text[Offset] == 'g' && Text[Offset + 1u] == 't' &&
            Text[Offset + 2u] == 'k' && Text[Offset + 3u] == 'm' && Text[Offset + 4u] == 'o' &&
            Text[Offset + 5u] == 'd' && Text[Offset + 6u] == 'e' && Text[Offset + 7u] == '=') {
            UINT32 Value = 0;
            UINTN Index = Offset + 8u;

            while (Index < Got && Text[Index] >= '0' && Text[Index] <= '9') {
                Value = Value * 10u + (UINT32)(Text[Index] - '0');
                Index++;
            }
            gAicGtkProbeMode = Value;
            SdhciMark("WiFi: probe: gtkmode=");
            SdhciMarkDecimal("", gAicGtkProbeMode);
            SdhciMark("\n");
        }
        if (Offset + 10u < Got && Text[Offset] == 'p' && Text[Offset + 1u] == 'o' &&
            Text[Offset + 2u] == 'r' && Text[Offset + 3u] == 't' && Text[Offset + 4u] == 'e' &&
            Text[Offset + 5u] == 'a' && Text[Offset + 6u] == 'r' && Text[Offset + 7u] == 'l' &&
            Text[Offset + 8u] == 'y' && Text[Offset + 9u] == '=') {
            gAicControlPortEarlyOpen = (Text[Offset + 10u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: portearly=");
            SdhciMarkDecimal("", gAicControlPortEarlyOpen);
            SdhciMark("\n");
        }
        if (Offset + 6u < Got && Text[Offset] == 'n' && Text[Offset + 1u] == 'o' &&
            Text[Offset + 2u] == 'g' && Text[Offset + 3u] == 't' && Text[Offset + 4u] == 'k' &&
            Text[Offset + 5u] == '=') {
            gAicSkipGroupKeyInstall = (Text[Offset + 6u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: nogtk=");
            SdhciMarkDecimal("", gAicSkipGroupKeyInstall);
            SdhciMark("\n");
        }
        if (Offset + 8u < Got && Text[Offset] == 'n' && Text[Offset + 1u] == 'o' &&
            Text[Offset + 2u] == 'a' && Text[Offset + 3u] == 'u' && Text[Offset + 4u] == 't' &&
            Text[Offset + 5u] == 'h' && Text[Offset + 6u] == 'z' && Text[Offset + 7u] == '=') {
            gAicSkipAuthorizedAfterKeys = (Text[Offset + 8u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: noauthz=");
            SdhciMarkDecimal("", gAicSkipAuthorizedAfterKeys);
            SdhciMark("\n");
        }
        if (Offset + 9u < Got && Text[Offset] == 'g' && Text[Offset + 1u] == 't' &&
            Text[Offset + 2u] == 'k' && Text[Offset + 3u] == 'f' && Text[Offset + 4u] == 'i' &&
            Text[Offset + 5u] == 'r' && Text[Offset + 6u] == 's' && Text[Offset + 7u] == 't' &&
            Text[Offset + 8u] == '=') {
            gAicGroupKeyBeforePairwise = (Text[Offset + 9u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: gtkfirst=");
            SdhciMarkDecimal("", gAicGroupKeyBeforePairwise);
            SdhciMark("\n");
        }
        if (Offset + 6u < Got && Text[Offset] == 'n' && Text[Offset + 1u] == 'o' &&
            Text[Offset + 2u] == 'p' && Text[Offset + 3u] == 't' && Text[Offset + 4u] == 'k' &&
            Text[Offset + 5u] == '=') {
            gAicSkipPairwiseKeyInstall = (Text[Offset + 6u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: noptk=");
            SdhciMarkDecimal("", gAicSkipPairwiseKeyInstall);
            SdhciMark("\n");
        }
        if (Offset + 9u < Got && Text[Offset] == 'g' && Text[Offset + 1u] == 't' &&
            Text[Offset + 2u] == 'k' && Text[Offset + 3u] == 'r' && Text[Offset + 4u] == 'e' &&
            Text[Offset + 5u] == 'a' && Text[Offset + 6u] == 'd' && Text[Offset + 7u] == 'd' &&
            Text[Offset + 8u] == '=') {
            gAicGroupKeyAddAgainAfterBoth = (Text[Offset + 9u] == '1') ? 1u : 0u;
            SdhciMark("WiFi: probe: gtkreadd=");
            SdhciMarkDecimal("", gAicGroupKeyAddAgainAfterBoth);
            SdhciMark("\n");
        }
    }
}
