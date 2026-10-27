/*
 * XHCI.h — USB 3.0 xHCI 主机控制器驱动接口
 *
 * 初始化控制器、枚举 USB 键盘。开机默认轮询事件环收 HID；MSI-X 不是默认。
 */
#ifndef XHCI_H
#define XHCI_H

#include "Compiler.h"

#include "BootTypes.h"

struct USB_CONTROLLER;

/* USB 控制传输 SETUP 阶段数据包 */
typedef struct {
    UINT8  bmRequestType;
    UINT8  bRequest;
    UINT16 wValue;
    UINT16 wIndex;
    UINT16 wLength;
} BLOCKS_PACKED USB_SETUP_PACKET;

/* GET_DESCRIPTOR wValue（高字节=描述符类型）；EHCI/xHCI 共用 */
#define USB_WVALUE_DT_DEVICE  0x0100u /* Device */
#define USB_WVALUE_DT_CONFIG  0x0200u /* Configuration */
#define USB_WVALUE_DT_HUB     0x2900u /* Hub class descriptor */
#define USB_HID_REPORT_IN     0x0100u /* HID GET_REPORT(Input)；数值同 DT_DEVICE，语义不同 */
#define USB_CDC_DTR_RTS       0x0003u /* SET_CONTROL_LINE_STATE: DTR|RTS */

/* USB 设备描述符（部分字段） */
typedef struct {
    UINT8  bLength;
    UINT8  bDescriptorType;
    UINT16 bcdUSB;
    UINT8  bDeviceClass;
    UINT8  bDeviceSubClass;
    UINT8  bDeviceProtocol;
    UINT8  bMaxPacketSize0;
    UINT16 idVendor;
    UINT16 idProduct;
    UINT16 bcdDevice;
    UINT8  iManufacturer;
    UINT8  iProduct;
    UINT8  iSerialNumber;
    UINT8  bNumConfigurations;
} BLOCKS_PACKED USB_DEVICE_DESCRIPTOR;

/* HID 键盘 8 字节引导协议报告 */
#ifndef USB_KEYBOARD_REPORT_DEFINED
#define USB_KEYBOARD_REPORT_DEFINED
typedef struct {
    UINT8  ModifierKeys;
    UINT8  Reserved;
    UINT8  KeyCode[6];
} BLOCKS_PACKED USB_KEYBOARD_REPORT;
#endif

/* USB 鼠标/平板报告；Absolute=1 时 X/Y 为 HID 0..32767 */
#ifndef USB_MOUSE_REPORT_DEFINED
#define USB_MOUSE_REPORT_DEFINED
typedef struct {
    UINT32 X;
    UINT32 Y;
    UINT8  Buttons;
    INT8   Wheel;
    UINT8  Absolute;
} USB_MOUSE_REPORT;
#endif

/*
 * 真机输入模式（PR-H-xhci-base → dual → irq）
 * POLL：零 MSI，Drain 盲排空（产品默认 + dual 失败备份）
 * DUAL：MSI-X 已武装，但 Drain 仍盲排空作 backup；不通则 FallbackToPoll
 * IRQ ：纯中断（可选，dual 证明后再开）
 */
typedef enum {
    XHCI_IRQ_MODE_POLL = 0,
    XHCI_IRQ_MODE_DUAL = 1,
    XHCI_IRQ_MODE_IRQ = 2
} XHCI_IRQ_MODE;

int HidKeyboardReady(void); /* 已 Address+Configure 键盘 */
/* 真机多 xHCI：当前控制器无 HID 时停 RS 并清 started，便于试下一颗 BAR */
void AbandonNoHid(void);
int EnableIrq(struct USB_CONTROLLER *Device);
/* PR-H-xhci-dual：试进 DUAL（MSI-X/MSI + Drain backup）；失败则 FallbackToPoll */
int TryEnterDual(struct USB_CONTROLLER *Device);
/* PR-H-xhci-irq：仅当 gStatIrq 已证明投递后，Drain 内懒升为轻量 IRQ 模式 */
/* 真机：PHOTO 后再枚举鼠标，避免踩键盘 IN */
void InitMouseDeferred(void);
/* PHOTO→桌面：清空鼠队列、重置累加坐标、Sync+Queue 键鼠 */
void MouseHandoffDesktop(UINT32 CursorX, UINT32 CursorY);
/* dual/irq 失败时切回 poll 备份 */
void FallbackToPoll(const char *Why);
XHCI_IRQ_MODE IrqMode(void);
void XhciDiagFormat(char *Buf, int Max); /* PHOTO/Shell：mode= + t/i/k/m/u/s/c/r/d/q/x */
void DiagLogArms(void);              /* slot/DCI 期望值 */
void Irq(void);
void DrainEvents(void);
int DequeueKeyboard(USB_KEYBOARD_REPORT *Report);
int MousePresent(void);
int DequeueMouse(USB_MOUSE_REPORT *Report);
int UsesIrq(void);
int KeyboardSetLeds(UINT8 Leds);
UINT8 KeyboardInterface(void);

/* 串口：短名在表上。实现在 Serial/Ftdi、Serial/Cdc 里是 static。 */
typedef struct {
    int (*Claim)(void);
    int (*Ready)(void);
    void (*Write)(const char *Text);
    void (*PollReceive)(void);
    int (*DataReady)(void);
    char (*ReadChar)(void);
} USB_UART_OPS;

extern USB_UART_OPS gFtdiUart;
extern USB_UART_OPS gCdcUart;
extern USB_UART_OPS gFtdiEhci;
void FtdiPublish(void);
void CdcPublish(void);

/* PR-N-wifi-1：RTL8188EU USB */
int XhciWifiClaim(void); /* 1 ok；0 无棒；-1 无 HC */
int XhciWifiReady(void);
UINT16 XhciWifiPid(void);

/* btx-1：板载/内部 USB HCI 软探针（见后 DisableSlot；Ready 仍 0） */
int XhciBluetoothProbe(void); /* 1 见过；0 无；-1 无 HC */
int XhciBluetoothSeen(void);
UINT16 XhciBluetoothVendorId(void);
UINT16 XhciBluetoothProductId(void);
UINT32 XhciBluetoothRootPort(void); /* 0=未见；MSC claim 须跳过该口 */

/* usb-hid-keep：Shell msc hot 解开 ClaimPorts 扫空/sick 冻结后再 Address */
void XhciPortEnumUnfreeze(void);
int XhciPortEnumIsFrozen(void);

#endif
