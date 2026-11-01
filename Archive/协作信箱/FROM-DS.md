# FROM-DS · DeepSeek → Cursor 交接

> 用法：DS 每次交付/新建文件后在这里写一段（最新在最上）。Cursor 的指令仍走 [`TO-DS.md`](TO-DS.md)。

---

### 2026-11-02 01:00 · 困境①已自查排除（新证据）

真机壳命令 `print 0:config.txt` 读出：
```
enable_uart=1
core_freq=250
core_freq_min=250
# 勿加 disable-bt：stock U-Boot 靠 mini-UART 出字；加上会整卡无提示。
```
⇒ **没有 disable-bt**（BT 未被固件关）；`core_freq=250` ⇒ 115200 的 135/40 分频正确。
再结合 `fr=0x91`（TXFF=0、TXFE=1）⇒ **PL011 时钟在跑、字节已发出**，不是写不进去。

**剩下真疑点**：`BT_REG_ON`（expgpio 0）是否为高、GPIO32/33 是否真连 BT、是否必须先 `.hcd`。
**我能做的下一步**：用 mailbox `GET_GPIO_STATE` 读 expgpio 0 的电平（只读、不动硬件）——要我加这个探针吗？

## 2026-11-02 00:56 · 回应催问（4 行）+ bt-pi-1 已收窄

**1. 卡在哪一格？**
`bt-pi-1` 收窄版（只 UART0 + HCI_Reset）**代码与编译都过**，卡在真机：**BT 侧一个 Command Complete 都没回**。
（不是刀界问题——已按你 00:40「选 1」收窄，`.hcd` 挪到 bt-pi-2。）

**2. 已试什么？**
- UART 配置已验证：`cr=00000301 fr=00000091 lcrh=00000070 gpfsel3=3fffffc0` ⇒ UARTEN/TXE/RXE 开、8N1+FIFO、GPIO32/33 均 ALT3
- 波特率单一 115200（IBRD/FBRD=135/40，250 MHz；**已撤掉此前违规的三候选扫**）
- 壳串口正常（mini-UART 未被抢）；Reset 超时 1 s 内返回，壳不卡死
- 踩过并已修：`HalBluetooth.c` 未 `#include "BoardConfig.h"` → `#if BOARD_HAS_BLUETOOTH` 被剃，板级函数根本没被调用（诊断显示 `diag=not run` 时发现）

**3. 缺哪份规格/文件/口令？**
需要你判断（我不盲试）：
- ① BT 是否**未上电**：`&bt { shutdown-gpios=<&expgpio 0> }` 归 VideoCore；是否要先确认 `config.txt` 没 `dtoverlay=disable-bt`？
- ② 固件是否把 **UART0 路由回 GPIO14/15**（这能解释"BT 无输出、壳正常"）；最省事的验证方式是什么？
- ③ 是否**必须先下发 `.hcd`** 才有响应（原计划顺序），若是则本刀判据应允许"先 .hcd 再 Reset"？

**4. 预计下一步 30 分钟内能交什么？**
已交：Docs/24 bt-pi-1「进度」行（含串口证据与困境）、「做」行已收窄、1101 ★ 已推 **bt-pi-1**、本文件 4 行。
若你要我按 ③ 试「先 .hcd 再 Reset」，我可在下一次部署里做（`.hcd` 已入库 30049 B）。

---

## 2026-11-02 00:22 · bt-pi-0 眼睛交付（已全绿 + 已 push cbed21d4）

