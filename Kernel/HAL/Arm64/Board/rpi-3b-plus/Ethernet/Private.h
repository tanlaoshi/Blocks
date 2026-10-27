/*
 * Ethernet/Private.h — LAN9514（smsc95xx）内部（bd-pi-eth）
 */
#ifndef BOARD_ETHERNET_PRIVATE_H
#define BOARD_ETHERNET_PRIVATE_H

#include "Ethernet.h"
/* 板头：勿用 "UsbHostController.h"（会命中 Include/Driver 公共口） */
#include "../UsbHostController.h"
#include "BoardConfig.h"
#include "Hal.h"
#include "HalDevices.h"
#include "Driver.h"
#include "DriverNet.h"
#include "Text.h"
#ifdef LWIP_ENABLE
#include "Netif.h"
#endif

#define SMSC_VID 0x0424u
#define SMSC_PID_LAN9514_ETH 0xec00u

#define USB_VENDOR_WRITE_REG 0xA0u
#define USB_VENDOR_READ_REG 0xA1u

#define ID_REV 0x00u
#define INT_STS 0x08u
#define TX_CFG 0x10u
#define TX_CFG_ON 0x00000004u
#define HW_CFG 0x14u
#define HW_CFG_LRST 0x00000008u
#define HW_CFG_RXDOFF 0x00000600u
#define PM_CTRL 0x20u
#define PM_CTL_PHY_RST 0x00000010u
#define LED_GPIO_CFG 0x24u
#define LED_GPIO_CFG_LEDS 0x01110000u
#define AFC_CFG 0x2Cu
#define AFC_CFG_DEFAULT 0x00F830A1u
#define E2P_CMD 0x30u
#define E2P_CMD_BUSY 0x80000000u
#define E2P_CMD_READ 0x00000000u
#define E2P_CMD_ADDR 0x000001FFu
#define E2P_DATA 0x34u
#define BURST_CAP 0x38u
#define INT_EP_CTL 0x68u
#define INT_EP_CTL_PHY_INT 0x00008000u
#define BULK_IN_DLY 0x6Cu
#define DEFAULT_BULK_IN_DELAY 0x00002000u
#define MAC_CR 0x100u
#define MAC_CR_TXEN 0x00000008u
#define MAC_CR_RXEN 0x00000004u
#define MAC_CR_PRMS 0x00040000u
#define MAC_CR_MCPAS 0x00080000u
#define MAC_CR_HPFILT 0x00002000u
#define ADDRH 0x104u
#define ADDRL 0x108u
#define MII_ADDR 0x114u
#define MII_BUSY 0x01u
#define MII_WRITE 0x02u
#define MII_DATA 0x118u
#define FLOW 0x11Cu
#define VLAN1 0x120u
#define COE_CR 0x130u

#define TX_CMD_A_FIRST 0x00002000u
#define TX_CMD_A_LAST 0x00001000u
#define RX_STS_FL 0x3FFF0000u
#define RX_STS_ES 0x00008000u

#define SMSC_PHY_ID 1
#define MII_BMSR 1
#define BMSR_LSTATUS 0x0004u
#define PHY_INT_SRC 29
#define PHY_INT_MASK 30
#define PHY_INT_MASK_DEFAULT 0x0050u

#define RX_URB_SIZE 2048u
#define EEPROM_MAC_OFF 1u

extern int gSmscReady;
extern int gSmscBound;
extern int gSmscLinkWasUp;
extern int gSmscLwIpReceive;
extern UINT8 gSmscAddress;
extern UINT8 gSmscHub;
extern UINT8 gSmscPort;
extern UINT8 gSmscEndpointIn;
extern UINT8 gSmscEndpointOut;
extern UINT16 gSmscBulkMaxPacketSize;
extern UINT8 gSmscTransmitToggle;
extern UINT8 gSmscReceiveToggle;
extern UINT32 gSmscMacControl;
extern UINT8 gSmscMac[6];
extern UINT32 gSmscIp;
extern UINT32 gSmscTransmitDone;
extern UINT32 gSmscReceiveFrames;
extern UINT8 gSmscTransmitBuffer[8u + 1518u];
extern UINT8 gSmscReceiveBuffer[RX_URB_SIZE];

void SmscDelayMilliseconds(UINT32 Milliseconds);
void SmscSelectDevice(void);
int SmscWriteRegister(UINT32 Index, UINT32 Data);
int SmscReadRegister(UINT32 Index, UINT32 *OutValue);
int SmscMiiRead(int Index, UINT16 *OutValue);
int SmscMiiWrite(int Index, UINT16 Value);
int SmscEepromRead(UINT32 Offset, UINT8 *Destination, UINT32 Length);
int SmscMacInitialize(void);
void SmscHandleReceive(const UINT8 *Buffer, int Length);

#endif
