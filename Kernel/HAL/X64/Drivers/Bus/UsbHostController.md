# UsbHostController 与现有符号

公共口在 `Include/Driver/UsbHostController.h`。键盘灯走 `Control`。`InterruptQueue` 的管道为空是键盘，非空是鼠标。U 盘的 CBW、数据和 CSW 走 `BulkOn`，扇区命令仍留在两边的 BOT 里。

| 公共口 | xHCI 现在 | EHCI 现在 |
| --- | --- | --- |
| `Control` | `ControlXfer` | `ControlTransfer` |
| `InterruptQueue` | `QueueIntr` / `QueueMouseIntr`（现为轮询入队，还没有完成回调） | `HidArmIntr` |
| `Bulk` | 有 xHCI 时走 `XhciBulkXfer` | 没有 xHCI 时走 `BulkXfer` |
| `BulkOn` | U 盘 BOT 指定走 xHCI | U 盘 BOT 指定走 EHCI |
| `ClaimInterface` | HID/MSC 的认领仍在类枚举里，尚未收成这一调用 | 同理 |
| `Register` / `Unregister` | `XhciInit` 在控制器跑起来后登记；放弃无 HID 的控制器时注销 | `Setup` 找到 PCI EHCI 后 `Offer`。有 xHCI 时 `Bulk` 仍先走 xHCI |
