/*
 * Crypto.c — WPA2 组密钥安装的驱动侧胶水：调公共 WifiGtkExtract，落 gIwlGtk* 并打串口。
 *
 * 从哪读：IwlInstallGtk。
 * 别改：gtk=kd / gtk=ln / gtk=ok / gtk=no / gtk=bad 这些串口文案是对拍判据的一部分。
 * 为什么还留在这里：串口文案与 gIwlGtk* 是驱动状态，算法本体在 HAL/Common/WifiCrypto。
 */
#include "Iwl/Private.h"
#include "HalSerial.h"
#include "WifiCrypto/WifiEapol.h"

static void IwlLogGtkKeyData(UINT16 KeyDataLength)
{
    char Line[16];
    char Hex[12];
    int Index = 0;
    const char *Prefix = "gtk=kd=";

    while (*Prefix) {
        Line[Index++] = *Prefix++;
    }
    HalSerialFormatHex(Hex, KeyDataLength & 0xffu, 2);
    Line[Index++] = Hex[2];
    Line[Index++] = Hex[3];
    Line[Index] = 0;
    IwlLogStage(Line);
}

static void IwlLogGtkElement(const WIFI_GTK *Gtk)
{
    char Line[24];
    char Hex[12];
    int Index = 0;
    const char *Prefix = "gtk=ln=";

    while (*Prefix) {
        Line[Index++] = *Prefix++;
    }
    HalSerialFormatHex(Hex, Gtk->ElementLength, 2);
    Line[Index++] = Hex[2];
    Line[Index++] = Hex[3];
    Line[Index++] = ' ';
    Line[Index++] = 'i';
    Line[Index++] = 'd';
    Line[Index++] = '=';
    HalSerialFormatHex(Hex, Gtk->KeyId, 2);
    Line[Index++] = Hex[2];
    Line[Index++] = Hex[3];
    Line[Index++] = ' ';
    Line[Index++] = 'k';
    Line[Index++] = 'b';
    Line[Index++] = '=';
    HalSerialFormatHex(Hex, Gtk->KeyLength, 2);
    Line[Index++] = Hex[2];
    Line[Index++] = Hex[3];
    if (Gtk->AlternateValid) {
        Line[Index++] = '+';
    }
    Line[Index] = 0;
    IwlLogStage(Line);
}

void IwlInstallGtk(const UINT8 *Eapol, UINTN EapLen, const UINT8 Kek[16])
{
    WIFI_GTK Gtk;
    int Status;

    gIwlGtkAltOk = 0;
    gIwlGtkLen = 0;
    Status = WifiGtkExtract(Kek, Eapol, EapLen, gIwlGroupCipher, &Gtk);
    if (Status == WIFI_GTK_STATUS_SHORT || Status == WIFI_GTK_STATUS_RANGE) {
        IwlLogStage("gtk=no");
        return;
    }
    IwlLogGtkKeyData(Gtk.KeyDataLength);
    if (Status == WIFI_GTK_STATUS_UNWRAP) {
        IwlLogStage("gtk=bad");
        return;
    }
    if (Status == WIFI_GTK_STATUS_KEY_SHORT) {
        gIwlGtkId = Gtk.KeyId;
        IwlLogStage("gtk=no");
        return;
    }
    if (Status == WIFI_GTK_STATUS_ABSENT) {
        IwlLogStage("gtk=no");
        return;
    }
    gIwlGtkId = Gtk.KeyId;
    IwlEapolCopyN(gIwlGtk, Gtk.Key, Gtk.KeyLength);
    gIwlGtkLen = Gtk.KeyLength;
    if (Gtk.AlternateValid) {
        IwlEapolCopyN(gIwlGtkAlt, Gtk.Alternate, WIFI_GTK_ALTERNATE_LENGTH);
        gIwlGtkAltOk = 1;
    }
    IwlLogGtkElement(&Gtk);
    IwlLogStage("gtk=ok");
}
