/*
 * BluetoothPrivate.h — Pi 3B 板级蓝牙（bt-pi-1）
 *
 * 总线口径（Docs/26 §1/§2 已钉死）：
 *   BT HCI = PL011 UART0 @0x3F201000，GPIO32 TXD0 / GPIO33 RXD0（ALT3）
 *   壳     = UART1 mini-UART @0x3F215000，GPIO14/15（ALT5）—— 本目录不得碰
 *
 * 本刀（bt-pi-1）只做：UART 字节通路 + HCI_Reset 收 Command Complete。
 *   Ready 仍为 0（bt-pi-2 才置）；不加载 .hcd（bt-pi-2）。
 */
#ifndef RPI_BLUETOOTH_PRIVATE_H
#define RPI_BLUETOOTH_PRIVATE_H

#include "BootTypes.h"

/* PL011 寄存器（BCM2835 ARM Peripherals §13） */
#define PI_UART0_BASE        0x3F201000ULL
#define PI_UART0_DR          0x00u
#define PI_UART0_FR          0x18u
#define PI_UART0_IBRD        0x24u
#define PI_UART0_FBRD        0x28u
#define PI_UART0_LCRH        0x2Cu
#define PI_UART0_CR          0x30u
#define PI_UART0_ICR         0x44u

#define PI_UART0_FR_TXFF     (1u << 5)
#define PI_UART0_FR_RXFE     (1u << 4)
#define PI_UART0_CR_UARTEN   (1u << 0)
#define PI_UART0_CR_TXE      (1u << 8)
#define PI_UART0_CR_RXE      (1u << 9)
#define PI_UART0_LCRH_8N1    (3u << 5)   /* WLEN=8bit, no parity, 1 stop */
#define PI_UART0_LCRH_FIFO   (1u << 4)

/*
 * PL011 波特率：115200。钟是 firmware UART clock（mailbox id=2），
 * **不是** mini-UART 的 core_freq=250 MHz。
 * 默认 init_uart_clock=48e6 → IBRD=26 / FBRD=3（树莓派文档 / bare-metal 对表）。
 * 运行时优先 BoardMboxGetClockHz(BOARD_MBOX_CLOCK_UART) 再算分频。
 */
#define PI_UART0_BAUD_RATE           115200u
#define PI_UART0_BAUD_IBRD_48MHZ     26u
#define PI_UART0_BAUD_FBRD_48MHZ     3u
#define PI_UART0_DEFAULT_CLOCK_HZ    48000000u

#define PI_GPIO_BASE         0x3F200000ULL
#define PI_GPIO_GPFSEL3      0x0Cu
#define PI_GPIO_GPFSEL4      0x10u  /* pins 40-49 */
#define PI_GPIO_PIN32_SHIFT  6u
#define PI_GPIO_PIN33_SHIFT  9u
#define PI_GPIO_FSEL_MASK    7u
#define PI_GPIO_FSEL_ALT3    7u

/* HCI（对表 NUC HAL/X64/.../Bluetooth/Private.h） */
#define PI_HCI_PACKET_COMMAND        0x01u
#define PI_HCI_PACKET_EVENT          0x04u
#define PI_HCI_EVENT_COMMAND_COMPLETE 0x0Eu
#define PI_HCI_OPCODE_RESET          0x0C03u
#define PI_HCI_COMMAND_PARAM_MAX     255u
#define PI_HCI_EVENT_MAX             260u
#define PI_BT_RESPONSE_TIMEOUT_MS    1000u

int PiUartTransportInitialize(void);
int PiUartTransportInitializeWithDivisor(UINT32 IntegerDivisor, UINT32 FractionalDivisor);
void PiUartWriteByte(UINT8 Value);
int PiUartReadByte(UINT8 *Out, UINT32 TimeoutMs);
int PiUartReadAvailable(UINT8 *Buffer, UINT32 MaxSize);

int PiHciSendCommand(UINT16 Opcode, const UINT8 *Parameters, UINT8 ParameterLength,
                     UINT8 *OutStatus);

/* bt-pi-1 的判据入口：初始化 UART + 发 HCI_Reset */
int BoardBluetoothBringUpReset(void);
/* 诊断快照：[CR,FR,LCRH,IBRD,FBRD,GPFSEL3] */
void PiUartTransportSnapshot(UINT32 Out[6]);
UINT32 PiUartTransportClockHz(void);
/* 最近一次 bring-up 的结论（壳可打印） */
const char *BoardBluetoothDiagnosis(void);
UINT32 BoardBluetoothAttemptMask(void);
int BoardBluetoothSelfTest(void);

/* 方向 A：GPIO43 Alt0 = GPCLK2 32.768 kHz（对表 dts `gpclk2_gpio43`） */
int PiBluetoothClock2Enable32k(void);
/* 方向 B（A 之后再开）：BT_ON = 扩展脚 128 */
int PiBluetoothPowerEnable(void);

#endif
