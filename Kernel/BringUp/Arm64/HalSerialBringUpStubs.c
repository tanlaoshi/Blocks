/*
 * HalSerialBringUpStubs.c — BRINGUP=1：HalSerial 门面所需的空 GOP/字体
 *
 * 完整内核链 HalSerialGop.c + Font；此处不碰屏。
 */
#include "HalSerial/HalSerialPrivate.h"

void GopBannerOnce(void) {
}

void GopWrite(const char *Text) {
    (void)Text;
}

int FontSetById(UINT32 Id) {
    (void)Id;
    return 0;
}

UINT32 FontAdvanceX(void) {
    return 8;
}

UINT32 FontAdvanceY(void) {
    return 16;
}
