# UsbHc 与现有符号

公共口在 `Include/Driver/UsbHc.h`。本刀不改调用。下一刀才让 xHCI 注册 `USB_HC_OPS`。

| 公共口 | xHCI 现在 | EHCI 现在 |
| --- | --- | --- |
| `UsbHcControl` | `ControlXfer` | `EhciControlXfer` |
| `UsbHcIntrQueue` | `QueueIntr` / `QueueMouseIntr`（现为轮询入队，还没有 Done 回调） | `EhciHidArmIntr` |
| `UsbHcBulk` | `XhciBulkXfer` | `EhciBulkXfer` |
| `UsbHcClaimInterface` | HID/MSC 的 `SetupHidDevice`、`ParseMscBulk` 仍在类枚举里，尚未收成这一调用 | `EhciEnumHid` 等同理 |
| `UsbHcRegister` / `UsbHcUnregister` | 尚无。注册顺序预定先 `USB_HC_XHCI` 再 `USB_HC_EHCI` | 同左 |

`USB_SETUP` 字段与 `XHCI.h` 的 `USB_SETUP_PACKET` 相同，单独起名，避免类驱动包含 `XHCI.h`。
