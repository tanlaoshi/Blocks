/*
 * UartTransport.c — Duo S UART4 字节通路（BT HCI）
 *
 * 基址/焊盘：Docs/27；pinmux 对表 U-Boot cvi_board_init WIFI/BT 与
 *   cv181x_pinlist_swconfig.h UART2_*__UART4_*=5。
 * 波特率：25 MHz / (16 * 1.5M) → DLL=1（dtsi clock-frequency）；禁止扫波特率。
 * 别改：不碰 UART0（壳）。
 */
#include "BluetoothPrivate.h"
#include "Hal.h"

static volatile UINT32 *DuoBluetoothUartRegister(UINT32 Index) {
    return (volatile UINT32 *)(UINTN)(DUO_BT_UART_BASE +
                                      ((UINT64)Index << DUO_BT_UART_REG_SHIFT));
}

static void DuoBluetoothPinmuxSetUart4(void) {
    volatile UINT32 *Pinmux;
    static const UINT32 Offsets[4] = {
        DUO_BT_PINMUX_UART2_TX, DUO_BT_PINMUX_UART2_RTS, DUO_BT_PINMUX_UART2_RX,
        DUO_BT_PINMUX_UART2_CTS};
    UINT32 Index;
    UINT32 Value;

    for (Index = 0u; Index < 4u; Index++) {
        Pinmux = (volatile UINT32 *)(UINTN)(DUO_BT_PINMUX_BASE + Offsets[Index]);
        Value = *Pinmux;
        Value = (Value & ~DUO_BT_PINMUX_FUNC_MASK) | DUO_BT_PINMUX_FUNC_UART4;
        *Pinmux = Value;
    }
}

static void DuoBluetoothUartSetBaudDivisor(UINT32 Divisor) {
    volatile UINT32 *LineControl = DuoBluetoothUartRegister(DUO_BT_UART_LCR);
    UINT32 Line = *LineControl;

    *LineControl = Line | DUO_BT_UART_LCR_DLAB;
    *DuoBluetoothUartRegister(DUO_BT_UART_RBR_THR_DLL) = Divisor & 0xFFu;
    *DuoBluetoothUartRegister(DUO_BT_UART_IER_DLH) = (Divisor >> 8) & 0xFFu;
    *LineControl = DUO_BT_UART_LCR_8N1;
}

int DuoBluetoothUartTransportInitialize(void) {
    UINT32 Divisor;

    DuoBluetoothPinmuxSetUart4();
    *DuoBluetoothUartRegister(DUO_BT_UART_IER_DLH) = 0u;
    /*
     * 25e6/(16*1.5e6)≈1.041 → DLL=1（实际 ≈1.5625 Mbps，对表 dts 钟；
     * AIC 默认表写 1.5M，禁止另扫）。
     */
    Divisor = DUO_BT_UART_CLOCK_HZ / (16u * DUO_BT_UART_BAUD_RATE);
    if (Divisor < 1u) {
        Divisor = 1u;
    }
    DuoBluetoothUartSetBaudDivisor(Divisor);
    *DuoBluetoothUartRegister(DUO_BT_UART_IIR_FCR) =
        DUO_BT_UART_FCR_ENABLE_FIFO | DUO_BT_UART_FCR_CLEAR_RX_TX;
    *DuoBluetoothUartRegister(DUO_BT_UART_MCR) =
        DUO_BT_UART_MCR_RTS | DUO_BT_UART_MCR_AFCE;
    return 0;
}

void DuoBluetoothUartWriteByte(UINT8 Value) {
    UINT32 Guard = 100000u;

    while ((*DuoBluetoothUartRegister(DUO_BT_UART_LSR) & DUO_BT_UART_LSR_THR_EMPTY) ==
           0u) {
        if (Guard-- == 0u) {
            return;
        }
    }
    *DuoBluetoothUartRegister(DUO_BT_UART_RBR_THR_DLL) = (UINT32)Value;
}

int DuoBluetoothUartReadByte(UINT8 *Out, UINT32 TimeoutMs) {
    UINT32 Start = HalMilliseconds();

    if (Out == 0) {
        return -1;
    }
    for (;;) {
        if ((*DuoBluetoothUartRegister(DUO_BT_UART_LSR) & DUO_BT_UART_LSR_DATA_READY) !=
            0u) {
            *Out = (UINT8)(*DuoBluetoothUartRegister(DUO_BT_UART_RBR_THR_DLL) & 0xFFu);
            return 0;
        }
        if ((UINT32)(HalMilliseconds() - Start) >= TimeoutMs) {
            return -1;
        }
    }
}
