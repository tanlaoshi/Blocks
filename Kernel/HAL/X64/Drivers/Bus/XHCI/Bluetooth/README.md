# X64 USB HCI · 蓝牙鼠（BLE HOGP）

NUC 板载 HCI → LE → SMP Just Works → HOGP Notify → 第二 `INPUT_BACKEND`。  
柱规格：`Archive/柱规格/蓝牙鼠-罗技MXMaster.md`。柱史：`Archive/项目/进展/路线图1109.md`。活 ★：`Archive/路线图1110.md`。

## 壳（手测）

| 命令 | 说明 |
|---|---|
| `bt lescan` | LE 扫描 |
| `bt mouse <n\|addr>` | 一键连鼠 |
| `bt mouse` | **同会话重连**上次地址（勿需再扫） |
| `bt disconnect` | 断 LE + 拆 HOGP 会话 |

验收：拔鼠底蓝牙通道再开 → `bt mouse` → 光标恢复；全程晃 USB 鼠不冻。

## 鼠优先自查（改泵/连/通知前过一遍）

- [ ] 扫/连/等事件循环有 `ServiceHidCompletions`（或等价泵片）
- [ ] 热路径（ConsumeEvent / ConsumeAclIn）**禁止** `AclOutSend` / `ControlTransfer`（重入 HostPump）
- [ ] 参数更新只入队，泵尾 `BluetoothLeLinkMaintainService` 再发
- [ ] `HidInput` Poll **禁止**再 `HostPump`（与 Drain 双跑抢环）
- [ ] 断线走 `BluetoothMouseSessionTeardown`（Notify/发现/MTU），勿只清 handle
- [ ] 手测：USB 鼠空拖 + BT 鼠拖动同时绿
