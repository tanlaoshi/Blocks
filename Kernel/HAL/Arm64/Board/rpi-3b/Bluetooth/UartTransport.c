/*
 * UartTransport.c — PL011 UART0 字节通路（Pi 3B 蓝牙 HCI）
 *
 * 引脚/基址出处见 BluetoothPrivate.h 头部注释与 Docs/26 §2。
 * 波特率：按 mailbox UART clock（默认 48 MHz）算 IBRD/FBRD；
 *   禁止用 core_freq=250 的 mini-UART 分频（那会把 HCI 波特率打飞）。
 * 别改：
 *   · 只动 GPIO32/33（ALT3）与 UART0；**绝不碰 GPIO14/15**（壳的 mini-UART）；
 *   · 读超时要返回 -1，不能死等（HCI 无响应时壳还得活着）。
 */
#include "BluetoothPrivate.h"
#include "Board.h"
#include "Hal.h"

static UINT32 sPiUartClockHz = PI_UART0_DEFAULT_CLOCK_HZ;
static UINT32 sPiUartBaudInteger = PI_UART0_BAUD_IBRD_48MHZ;
static UINT32 sPiUartBaudFractional = PI_UART0_BAUD_FBRD_48MHZ;

static volatile UINT32 *PiUartRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_UART0_BASE + (UINT64)Offset);
}

static volatile UINT32 *PiGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_GPIO_BASE + (UINT64)Offset);
}

static void PiUartSetPinFunction(UINT32 Shift) {
    volatile UINT32 *Select = PiGpioRegister(PI_GPIO_GPFSEL3);
    UINT32 Value = *Select;

    Value &= ~(PI_GPIO_FSEL_MASK << Shift);
    Value |= (PI_GPIO_FSEL_ALT3 & PI_GPIO_FSEL_MASK) << Shift;
    *Select = Value;
}

static void PiUartSetBaud(UINT32 IntegerDivisor, UINT32 FractionalDivisor) {
    *PiUartRegister(PI_UART0_IBRD) = IntegerDivisor & 0xFFFFu;
    *PiUartRegister(PI_UART0_FBRD) = FractionalDivisor & 0x3Fu;
}

/*
 * PL011：BAUDDIV = FUARTCLK / (16 * baud)；IBRD=整数，FBRD=round(frac*64)。
 * 整数实现：scaled = round(FUARTCLK * 64 / (16 * baud))。
 */
static void PiUartComputeDivisors(UINT32 ClockHz, UINT32 *OutInteger,
                                  UINT32 *OutFractional) {
    UINT64 Denominator;
    UINT64 Scaled;

    if (OutInteger == 0 || OutFractional == 0) {
        return;
    }
    if (ClockHz < (16u * PI_UART0_BAUD_RATE)) {
        *OutInteger = PI_UART0_BAUD_IBRD_48MHZ;
        *OutFractional = PI_UART0_BAUD_FBRD_48MHZ;
        return;
    }
    Denominator = 16ull * (UINT64)PI_UART0_BAUD_RATE;
    Scaled = (((UINT64)ClockHz << 6) + (Denominator / 2ull)) / Denominator;
    if (Scaled < 1ull) {
        Scaled = 1ull;
    }
    *OutInteger = (UINT32)(Scaled >> 6);
    *OutFractional = (UINT32)(Scaled & 63ull);
    if (*OutInteger == 0u) {
        *OutInteger = 1u;
        *OutFractional = 0u;
    }
}

int PiUartTransportInitializeWithDivisor(UINT32 IntegerDivisor, UINT32 FractionalDivisor) {
    *PiUartRegister(PI_UART0_CR) = 0u;
    PiUartSetPinFunction(PI_GPIO_PIN32_SHIFT);
    PiUartSetPinFunction(PI_GPIO_PIN33_SHIFT);
    PiUartSetBaud(IntegerDivisor, FractionalDivisor);
    *PiUartRegister(PI_UART0_LCRH) = PI_UART0_LCRH_8N1 | PI_UART0_LCRH_FIFO;
    *PiUartRegister(PI_UART0_ICR) = 0x7FFu;
    *PiUartRegister(PI_UART0_CR) = PI_UART0_CR_UARTEN | PI_UART0_CR_TXE | PI_UART0_CR_RXE;
    return 0;
}

void PiUartWriteByte(UINT8 Value) {
    UINT32 Guard = 100000u;

    while ((*PiUartRegister(PI_UART0_FR) & PI_UART0_FR_TXFF) != 0u) {
        if (Guard-- == 0u) {
            return;
        }
    }
    *PiUartRegister(PI_UART0_DR) = (UINT32)Value;
}

int PiUartReadByte(UINT8 *Out, UINT32 TimeoutMs) {
    UINT32 Start = HalMilliseconds();

    if (Out == 0) {
        return -1;
    }
    for (;;) {
        if ((*PiUartRegister(PI_UART0_FR) & PI_UART0_FR_RXFE) == 0u) {
            *Out = (UINT8)(*PiUartRegister(PI_UART0_DR) & 0xFFu);
            return 0;
        }
        if ((UINT32)(HalMilliseconds() - Start) >= TimeoutMs) {
            return -1;
        }
    }
}

int PiUartReadAvailable(UINT8 *Buffer, UINT32 MaxSize) {
    UINT32 Count = 0u;

    if (Buffer == 0) {
        return -1;
    }
    while (Count < MaxSize &&
           (*PiUartRegister(PI_UART0_FR) & PI_UART0_FR_RXFE) == 0u) {
        Buffer[Count] = (UINT8)(*PiUartRegister(PI_UART0_DR) & 0xFFu);
        Count++;
    }
    return (int)Count;
}

int PiUartTransportInitialize(void) {
    UINT32 ClockHz = PI_UART0_DEFAULT_CLOCK_HZ;

    if (BoardMboxGetClockHz(BOARD_MBOX_CLOCK_UART, &ClockHz) != 0 || ClockHz == 0u) {
        ClockHz = PI_UART0_DEFAULT_CLOCK_HZ;
    }
    sPiUartClockHz = ClockHz;
    PiUartComputeDivisors(ClockHz, &sPiUartBaudInteger, &sPiUartBaudFractional);
    return PiUartTransportInitializeWithDivisor(sPiUartBaudInteger, sPiUartBaudFractional);
}

void PiUartTransportSnapshot(UINT32 Out[6]) {
    if (Out == 0) {
        return;
    }
    Out[0] = *PiUartRegister(PI_UART0_CR);
    Out[1] = *PiUartRegister(PI_UART0_FR);
    Out[2] = *PiUartRegister(PI_UART0_LCRH);
    Out[3] = *PiUartRegister(PI_UART0_IBRD);
    Out[4] = *PiUartRegister(PI_UART0_FBRD);
    Out[5] = *PiGpioRegister(PI_GPIO_GPFSEL3);
}

UINT32 PiUartTransportClockHz(void) {
    return sPiUartClockHz;
}
