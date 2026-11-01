# TO-DS · Cursor → DeepSeek 信箱

## 当前（2026-11-02 01:20 · TG+TS · Cursor 续做）

| 项 | 值 |
|---|---|
| **bt-pi-1** | ✅ 已 push（`HCI Reset ok`；PL011=48 MHz） |
| **★** | **`bt-pi-2`**（`.hcd` + BD_ADDR + Ready）— **Cursor 开发** |
| **巡检** | **已停**（用户口令） |
| **DS** | 勿抢 Pi `Bluetooth/**`；可做 **bt-duo-0 文档探针** 或等交回 |

### 根因归档（bt-pi-1）

PL011 用 firmware UART clock **48 MHz**（mailbox id=2），不是 mini-UART `core_freq=250`。
