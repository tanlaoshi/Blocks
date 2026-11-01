/*
 * BluetoothPrivate.h — Duo S 板级蓝牙（bt-duo-1）
 *
 * Docs/27：HCI = UART4 @0x041C0000（DW-APB，reg-shift=2）；
 *   U-Boot 将 UART2 焊盘 mux 成 UART4（含 CTS/RTS）；壳仍 UART0，本目录勿碰。
 * 电源与 WiFi 共用 GPIOA15（已由 Sdhci/Power 拉高时复用）。
 * bt-duo-2：SDIO 灌 fmacfwbt（固件阶段选路）；禁止改数据面 / netif。
 */
#ifndef DUO_BLUETOOTH_PRIVATE_H
#define DUO_BLUETOOTH_PRIVATE_H

#include "BootTypes.h"

#define DUO_BT_UART_BASE              0x041C0000ULL
#define DUO_BT_UART_REG_SHIFT         2u
#define DUO_BT_UART_CLOCK_HZ          25000000u /* cv181x_base.dtsi uart4 */
#define DUO_BT_UART_BAUD_RATE         1500000u  /* AICBT_UART_BAUD_1_5M */

#define DUO_BT_PINMUX_BASE            0x03001000ULL
#define DUO_BT_PINMUX_UART2_TX        0xC0u
#define DUO_BT_PINMUX_UART2_RTS       0xC4u
#define DUO_BT_PINMUX_UART2_RX        0xC8u
#define DUO_BT_PINMUX_UART2_CTS       0xCCu
#define DUO_BT_PINMUX_FUNC_UART4      5u /* UART2_*__UART4_* */
#define DUO_BT_PINMUX_FUNC_MASK       0x7u

/* 16550 寄存器索引（再 << REG_SHIFT） */
#define DUO_BT_UART_RBR_THR_DLL       0u
#define DUO_BT_UART_IER_DLH           1u
#define DUO_BT_UART_IIR_FCR           2u
#define DUO_BT_UART_LCR               3u
#define DUO_BT_UART_MCR               4u
#define DUO_BT_UART_LSR               5u

#define DUO_BT_UART_LCR_DLAB          0x80u
#define DUO_BT_UART_LCR_8N1           0x03u
#define DUO_BT_UART_FCR_ENABLE_FIFO   0x01u
#define DUO_BT_UART_FCR_CLEAR_RX_TX   0x06u
#define DUO_BT_UART_MCR_RTS           0x02u
#define DUO_BT_UART_MCR_AFCE          0x20u /* DW auto flow */
#define DUO_BT_UART_LSR_DATA_READY    0x01u
#define DUO_BT_UART_LSR_THR_EMPTY     0x20u

#define DUO_HCI_PACKET_COMMAND        0x01u
#define DUO_HCI_PACKET_EVENT          0x04u
#define DUO_HCI_EVENT_COMMAND_COMPLETE 0x0Eu
#define DUO_HCI_OPCODE_RESET          0x0C03u
#define DUO_HCI_OPCODE_WRITE_LOCAL_NAME 0x0C13u
#define DUO_HCI_OPCODE_READ_BD_ADDR   0x1009u
#define DUO_HCI_LOCAL_NAME_LENGTH     248u
#define DUO_HCI_EVENT_MAX             260u
#define DUO_BT_RESPONSE_TIMEOUT_MS    2000u
#define DUO_BT_ADDRESS_LENGTH         6u
#define DUO_BT_LOCAL_NAME_STORE       64u

int DuoBluetoothUartTransportInitialize(void);
void DuoBluetoothUartWriteByte(UINT8 Value);
int DuoBluetoothUartReadByte(UINT8 *Out, UINT32 TimeoutMs);

int DuoBluetoothHciSendCommand(UINT16 Opcode, const UINT8 *Parameters,
                               UINT8 ParameterLength, UINT8 *OutStatus,
                               UINT8 *OutReturn, UINT8 ReturnMax,
                               UINT8 *OutReturnLength);

int DuoBluetoothFirmwareLoad(void);

int BoardBluetoothBringUp(void);
int BoardBluetoothReady(void);
int BoardBluetoothCopyDeviceAddress(UINT8 Out[DUO_BT_ADDRESS_LENGTH]);
int BoardBluetoothCopyLocalName(char *Out, UINTN NameMax);
const char *BoardBluetoothDiagnosis(void);

#endif
