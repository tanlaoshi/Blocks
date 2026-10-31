/*
 * KsendCrc.c — CRC-32/ISO-HDLC（反射，poly 0xEDB88320，初值/终值异或全 1）
 *
 * 与 PC 侧 Python zlib.crc32 同参，因此：
 *   块 CRC   = Final(Update(INITIAL, Data, Len))
 *   整体 CRC = Final(Update(... 继续喂 ...))
 * 逐位实现（不查表）：整趟传输要算 ~3 遍 2.2 MB，C906 上仍是几十毫秒量级，
 * 换掉 1 KB 常量表更划算；将来嫌慢再换 nibble 表。
 */
#include "Ksend/Private.h"

UINT32 KsendCrc32Update(UINT32 Crc, const UINT8 *Data, UINT32 Length) {
    UINT32 Index;
    UINT32 Bit;

    for (Index = 0; Index < Length; Index++) {
        Crc ^= (UINT32)Data[Index];
        for (Bit = 0; Bit < KSEND_CRC_BITS_PER_BYTE; Bit++) {
            if ((Crc & 1u) != 0u) {
                Crc = (Crc >> 1) ^ KSEND_CRC_POLYNOMIAL;
            } else {
                Crc = Crc >> 1;
            }
        }
    }
    return Crc;
}

UINT32 KsendCrc32Final(UINT32 Crc) {
    return Crc ^ KSEND_CRC_FINAL_XOR;
}
