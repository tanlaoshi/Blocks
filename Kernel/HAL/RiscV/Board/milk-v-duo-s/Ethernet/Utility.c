/*
 * Utility.c — 内存 / 字节序 / MMIO / 延时 / BootMark（bd-duo-eth）
 */
#include "Ethernet/Private.h"

void DuoMemoryCopy(void *Destination, const void *Source, UINTN Length) {
    UINT8 *Out = (UINT8 *)Destination;
    const UINT8 *In = (const UINT8 *)Source;
    UINTN Index;

    for (Index = 0; Index < Length; Index++) {
        Out[Index] = In[Index];
    }
}

void DuoMemorySet(void *Destination, UINT8 Value, UINTN Length) {
    UINT8 *Out = (UINT8 *)Destination;
    UINTN Index;

    for (Index = 0; Index < Length; Index++) {
        Out[Index] = Value;
    }
}

UINT16 DuoByteSwap16(UINT16 Value) {
    return (UINT16)((Value << 8) | (Value >> 8));
}

UINT32 DuoByteSwap32(UINT32 Value) {
    return ((Value & 0x000000FFu) << 24) | ((Value & 0x0000FF00u) << 8) |
           ((Value & 0x00FF0000u) >> 8) | ((Value & 0xFF000000u) >> 24);
}

volatile UINT32 *DuoRegister(UINT64 Address) {
    return (volatile UINT32 *)(UINTN)Address;
}

void DuoSpin(UINT32 Count) {
    volatile UINT32 Index;

    for (Index = 0; Index < Count; Index++) {
    }
}

void DuoDelayMilliseconds(UINT32 Milliseconds) {
    UINT32 Index;

    for (Index = 0; Index < Milliseconds; Index++) {
        DuoSpin(80000u);
    }
}

void DuoMarkHex(const char *Tag, UINT32 Value) {
    char Buffer[9];

    HalSerialBootMark(Tag);
    HalSerialFormatHex(Buffer, Value, 8);
    HalSerialBootMark(Buffer);
}

void DuoMarkHex16(UINT32 Value) {
    char Buffer[5];

    HalSerialFormatHex(Buffer, Value, 4);
    HalSerialBootMark(Buffer);
}

int DuoVersionIsLive(UINT32 Version) {
    return Version != 0u && Version != 0xFFFFFFFFu;
}
