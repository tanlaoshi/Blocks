/*
 * LibcStub.c — freestanding 补 lwIP 所需宿主符号（atoi）
 *
 * ThirdParty/lwip netif.c 调 atoi；内核 -nostdlib 无 libc。
 */
#include "BootTypes.h"

int atoi(const char *Text) {
    int Sign = 1;
    int Value = 0;

    if (Text == 0) {
        return 0;
    }
    while (*Text == ' ' || *Text == '\t') {
        Text++;
    }
    if (*Text == '-') {
        Sign = -1;
        Text++;
    } else if (*Text == '+') {
        Text++;
    }
    while (*Text >= '0' && *Text <= '9') {
        Value = Value * 10 + (*Text - '0');
        Text++;
    }
    return Sign * Value;
}
