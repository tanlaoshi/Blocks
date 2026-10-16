# UsbHostController 与现有符号

公共口在 `Include/Driver/UsbHostController.h`。键盘、鼠标、U 盘仍走原来的函数。

| 公共口 | xHCI 现在 | EHCI 现在 |
| --- | --- | --- |
| `Control` | `ControlXfer` | `EhciControlXfer` |
| `InterruptQueue` | `QueueIntr` / `QueueMouseIntr`（现为轮询入队，还没有完成回调） | `EhciHidArmIntr` |
| `Bulk` | `XhciBulkXfer` | `EhciBulkXfer` |
| `ClaimInterface` | HID/MSC 的 `SetupHidDevice`、`ParseMscBulk` 仍在类枚举里，尚未收成这一调用 | `EhciEnumHid` 等同理 |
| `Register` / `Unregister` | `XhciInit` 在控制器跑起来后 `XhciUsbHostControllerOffer`；放弃无 HID 的控制器时注销 | `EhciSetup` 找到 PCI EHCI 后 `Offer`。有 xHCI 时公共口仍先走 xHCI |

`USB_SETUP` 字段与 `XHCI.h` 的 `USB_SETUP_PACKET` 相同，单独起名，避免类驱动包含 `XHCI.h`。
