/*
 * Serial.c — BootStub UART0（DW 16550，reg-shift=2）直写
 */
#include "BoardConfig.h"
#include "BootTypes.h"
#include "HalSerial.h"
#include "LibWrite.h"

#define UART_THR (0u << BOARD_UART_REG_SHIFT)
#define UART_LSR (5u << BOARD_UART_REG_SHIFT)
#define UART_LSR_THRE 0x20u

static volatile UINT8 *SerialRegister(UINT32 Offset) {
    return (volatile UINT8 *)(UINTN)(BOARD_UART_BASE + (UINT64)Offset);
}

void BootStubSerialWrite(const char *Text) {
    if (Text == 0) {
        return;
    }
    while (*Text) {
        while ((SerialRegister(UART_LSR)[0] & UART_LSR_THRE) == 0) {
        }
        SerialRegister(UART_THR)[0] = (UINT8)*Text++;
    }
}

/* Utility.c SdhciMark 默认出口 */
void HalSerialWriteShell(const char *Text) {
    BootStubSerialWrite(Text);
}

void BootStubSerialInitialize(void) {
    LibWriteRegister(BootStubSerialWrite);
}

void HalCpuRelax(void) {
    __asm__ volatile("nop");
}

void HalSerialFormatHex(char *Buffer, UINT64 Value, int Digits) {
    static const char Hex[] = "0123456789abcdef";
    int Index;

    if (Buffer == 0) {
        return;
    }
    if (Digits < 1) {
        Digits = 1;
    }
    if (Digits > 16) {
        Digits = 16;
    }
    for (Index = Digits - 1; Index >= 0; Index--) {
        Buffer[Index] = Hex[Value & 0xFu];
        Value >>= 4;
    }
    Buffer[Digits] = 0;
}
