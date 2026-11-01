# Duo Sdhci / WiFi SDIO 代码 Review

> 对象：`Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/`（含 `Sdhci.h`）。  
> 口径：DeepSeek 写的 Duo WiFi **总线层**（PR-30 S1–S3 范围）。  
> 依据：对照 [`DUO-WiFi-SDIO时序.md`](DUO-WiFi-SDIO时序.md)、SG2000 TRM 18.4、厂商 `sdhci-cv181x.c` / `aicsdio.c`、Linux `sdio_ops.c` / `sdio_cis.c`，以及本仓命名/行数铁律。  
> 只读评审，**未改一行代码**；结论带 `文件:行号`。  
> 标记：⚠️ = 会改验收语义 / 把假绿当真绿 / 污染芯片状态；🟡 = 隐患与卫生（当前不一定发作）；ℹ️ = 范围说明（不是 bug，但别当 WiFi 驱动）。

| 节 | 可疑点 | ⚠️ | 🟡 |
| --- | --- | --- | --- |
| 1 范围 | R1 | — | — |
| 2 验收与状态 | A1–A3（3） | 2 | 1 |
| 3 日志 | L1（1） | 1 | 0 |
| 4 协议细节 | P1–P4（4） | 0 | 4 |
| 5 板级 / 启动 | B1–B2（2） | 0 | 2 |
| 6 结构卫生 | S1–S2（2） | 0 | 2 |
| 合计 | 12 | 3 | 9 |

---

## 0 · 结论先行

1. **总线层骨架大体对。** SD0/SD1 多实例、`CMD5→3→7`、CMD52/53 参数位、先卡后主机的 4-bit、HS（EHS→时钟→`HOST_CONTROL[2]`）、`MSHC_CTRL[16]`、不进 1.8 V、`INT_ENABLE` 必须打开——这些与时序表和真机踩坑注释一致，能当 SDIO 枚举底座。
2. **它还不是 WiFi 驱动。** 没有固件下载（S4）、没有 fn1 `0x04` 轮询收包、没有中断、没有 802.11。`SdhciWifiInitialize` 成功只证明「SDIO 枚举 + CCCR/CIS 读通」；别把 `Host->Ready=1` 当成网卡可用。
3. **最该先修的是验收语义，不是再堆寄存器。** ① FIFO「写图案→读回比相等」不是 S3 判据，还可能污染发送 FIFO；② 自检失败仍返回成功；③ `SdhciMarkHex` 仍走 `HalSerialBootMark`，`SERIAL_BOOT=0` 时 CIS/OCR/状态字证据行消失。这三条比「再调一次时钟」更致命。
4. **S3 的真验收**仍是时序表 §6：`WiFi: CIS manfid=c8a1:0082`（以及 CCCR/HS/4bit 行）。FIFO 回环、探测态 `Ready`、开机里同步堵两秒，都不能替代这条。

---

## 1 · 范围（先钉死评什么）

### R1 · ℹ️ 这是 SDIO 总线探针，不是 AIC8800 驱动

| 已有 | 未有（留给 S4+） |
| --- | --- |
| SD1 @ `0x04320000`、时钟门控、GPIOA15 上电、TOP `0x294` CD 覆盖 | 固件 blob 下载 / `DBG_MEM_*` |
| CMD0/5/3/7、CMD52/53、CCCR、4-bit、HS≈50 MHz | DAT1 卡中断 + PLIC 38 |
| CIS MANFID、fn1 使能、块大小 | fn1 `0x04` → FIFO 读循环 |
| PIO 多块 / 字节模式骨架 | 流控读 fn1 `0x03`、帧对齐（TAIL_LEN / 512） |

`Sdio.c:11` 自己写了「本轮不做中断、不做固件下载」——评审按这句话收口，不拿「扫不到 AP」骂这一层。

---

## 2 · 验收与状态（⚠️ 优先）

### A1 · ⚠️ FIFO 自检把 TX/RX FIFO 当成回环内存

- **位置**：`Sdhci/Sdio.c:156-205`（`SdioFifoTransfer` / `SdioFifoTest`），在 `SdhciWifiInitialize` 末尾 `Sdio.c:650-651` 调用。
- **现状**：往 fn1 `0x10`（写 FIFO）灌 1536 B 图案，再从 fn1 `0x0F`（读 FIFO）读回比相等。
- **为何错**：时序表 §2.6——写长度 1536 是**固件消息补齐后的线上长度**；读长度来自 fn1 `0x04` 的待收块数；写前还要读 fn1 `0x03` 流控。图案回读对不上是正常现象。更糟的是：协议开始前就把 1536 B 垃圾推进发送 FIFO，可能弄脏后续 S4 的第一条消息。
- **S3 该看什么**：`WiFi: CIS manfid=c8a1:0082`（时序表 §6），不是 `FIFO 1536B rw ok`。
- **建议**：删掉或降级为「只测 CMD53 单次短传输、不比内容」；真要测数据相，跟厂商一样先读 `0x04` 再决定读多长，且**不要在枚举成功路径上默认灌 FIFO**。

### A2 · ⚠️ 自检失败仍报 probe 成功

- **位置**：`Sdio.c:183-204` 失败只 `SdhciFail` / 打 diff；`Sdio.c:658-662` 仍 `Host->CardType=SDIO`、`Host->Ready=1`、`gWifiSdioState=1`、`return 0`。
- **后果**：串口可以同时出现 `FIFO … fail` 和 `WiFi: probe end ok`。上层若只看返回值 / `Ready`，会把半截总线当成可用。
- **建议**：要么 FIFO 测试移出成功路径，要么失败则 `gWifiSdioState=-1` 且 `return -1`（并明确：CIS 已过、FIFO 未过——不要混成一个 bool）。

### A3 · 🟡「只试一次」把失败闩死，强制重试靠 shell

- **位置**：`gWifiSdioState`（`Sdio.c:57`）；`SdhciWifiInitialize` 入口 `Sdio.c:330-334`；`SdhciWifiProbe` 的 `FORCE` 清状态（`Sdio.c:292-309`）。
- **意图合理**（避免开机刷屏），但和 A2 叠在一起时：一次假绿或一次可恢复失败都会闩住，直到人手 `wifiprobe`。
- **建议**：成功闩可以留；失败闩应允许「上电/时钟门控变更后再试」，或默认失败不闩、只限频日志。

---

## 3 · 日志

### L1 · ⚠️ `SdhciMarkHex` 没走新出口

- **位置**：`Utility.c:88-97`（`SdhciMark` → `HalSerialWriteShell` / sink）；`Utility.c:99-107`（`SdhciMarkHex` → **`HalSerialBootMark`**）。
- **后果**：注释写明改直写 UART 是为了 `SERIAL_BOOT=0` 仍看得见；但 CIS / OCR / `INT_STATUS` / `PRESENT_STATE` / CCCR 等**证据行全是 Hex**。通道关时明文还在、十六进制消失——`wifiprobe` 排障正好缺字段。
- **建议**：`SdhciMarkHex` / `SdhciMarkDecimal` 统一走 `SdhciMark`（或同一 sink），禁止 Hex 单独走 Boot 通道。

---

## 4 · 协议细节（🟡）

### P1 · 🟡 CMD53 的 R5 检查太晚

- **位置**：`Io.c:117-130`——先 `SdhciCommandSend` + 整段 PIO，最后才 `SdhciR5Check`。
- **问题**：R5 在 `INT_RESPONSE` 时已在 `Host->Response[0]`。卡已用 `OUT_OF_RANGE` / `FUNCTION_NUMBER` 拒绝时，仍会把数据相跑完，超时/复位噪声变大。
- **建议**：命令响应返回后、PIO 前先查 R5；数据相后再查一次亦可（注释已提到传输期置位的可能）。

### P2 · 🟡 字节模式路径未写 `AIC_BYTEMODE_ENABLE_REG_V3`

- **位置**：宏在 `Sdio.c:40`；`SdioFifoTest` 在 `SMB=0` 时走字节模式（`Sdio.c:651`），从未写 fn1 `0x07`。
- **对照**：时序表 §2.5 / 厂商表——字节模式开关在 fn1 `0x07`（D80）。CCCR `SMB=1` 时碰不到；一旦卡报不支持多块，自检/后续传输会 silently 走错模式。
- **建议**：进入字节模式 CMD53 前按厂商写该寄存器；或 S3 验收明确要求 `SMB=1`，SMB=0 直接失败并打日志。

### P3 · 🟡 `CISTPL_FUNCE` 按 function 0 布局解析

- **位置**：`Sdio.c:129-135`——`Length >= 3` 就取 `Data[1]|Data[2]<<8` 当 `MaxBlockSize`。
- **问题**：CIS 里 fn0 与 fn1+ 的 FUNCE 布局不同；当前主要用于日志 `blk=`，**不影响 MANFID 验收**。
- **建议**：S3 只信任 MANFID；`blk=` 要么按 tuple 类型分支，要么打 raw length 别解释错。

### P4 · 🟡 `SdhciClockConfigure` 在极端分频下可能除零

- **位置**：`Command.c:70-71`（`Hertz >= SD_BASE_CLOCK_HERTZ` → `Div=1` 再 `Div>>=1` → 0）；`Command.c:97-99` 用 `2u * Div`。
- **现状**：现有调用是 400 kHz / 25 MHz / 50 MHz，碰不到；属于潜伏。
- **建议**：`Div==0` 时按「不分频」处理实际频率，或拒绝 `Hertz >= base`。

---

## 5 · 板级 / 启动

### B1 · 🟡 卡检测极性自测改了 TOP，却不当门禁

- **位置**：`Power.c:202-240`（`DuoWifiCardDetect`）。
- **现状**：`cd_sw=1` / `0` 各试一次；两种都不置 `PRESENT_STATE[16]` 时恢复原值并继续。注释正确指出 Linux non-removable 不看这位。
- **风险**：改 `0x294` 的窗口里若并发/后续代码误信「已覆盖」，或恢复失败，会留下与厂商固件不一致的 TOP 状态。功能上目前「只打印」——可接受，但别升级成硬依赖 `[16]`。
- **建议**：保持「只读/覆盖失败才 -1」；极性实验若已有真机结论，收成常量，去掉来回写。

### B2 · 🟡 WiFi 探针同步挂在 SD0 块设备 Probe 里

- **位置**：`Sdhci.c:88-95`——SD0 `SdhciHostInitialize` 之后无条件 `(void)SdhciWifiInitialize()`。
- **后果**：模块无响应时 CMD5 窗口约 2 s（`Private.h:58-61`，`SdioCmd5Window`），**拖住启动盘探测返回**。WiFi 失败虽不改块设备 Rc，但拉长「盘 ready」时间。
- **建议**：开机只做「可选、可跳过、可异步」；或默认跳过、shell `wifiprobe` / 显式 flag 再跑（与 A3 的 FORCE 模型对齐）。

---

## 6 · 结构卫生（本仓铁律）

### S1 · 🟡 `Sdio.c` 远超单文件 300 行

- **现状**：`Sdio.c` ≈ 663 行；`SdhciWifiInitialize` 单函数 ≈ 350 行。
- **铁律**：自有 `.c` ≤300；将满先拆。板债拆分刀（`bd-duo-sd`）管过旧 `Sdhci.c`，**新堆的 WiFi 枚举同属自有代码，不豁免**。
- **建议拆法（示意）**：`SdioCis.c`（指针+tuple）、`SdioEnumerate.c`（CMD5 窗+CCCR/HS/4bit）、门面只留 `SdhciWifiInitialize` / `SdhciWifiProbe`；FIFO 测试若保留，单独文件且勿默认跑。

### S2 · 🟡 命名总体合规，少数缩写边界

- 公开/内部长名大体遵守「不砍字」（`SdhciIoRwExtended`、`DuoWifiCardDetect`、`SdhciPhysicalAfterReset` 等）。
- 局部仍见日志/状态缩写习惯（`pst`/`ien`/`ocr` 作日志标签可接受；类型字段 `Cis`、`Caps` 作局部变量需注意别扩散成文件/API 名）。
- **收口门禁**：下一刀碰这些文件时，按 `.cursor/rules/name-loc-selfcheck.mdc` 再过一遍，禁止再引入 `Util`/`Cfg`/`Msg` 类文件名。

---

## 7 · 写对的部分（避免误伤）

下面这些**不要当成问题重开**：

| 点 | 依据 |
| --- | --- |
| SD0/SD1 共代码、块设备只绑 SD0 | `Sdhci.c` + `SDHCI_HOST` |
| CMD5 R4：无 CRC / 无 index | `Sdio.c:428`；`sdio_ops.c` |
| 先开 `INT_ENABLE=0xFFFFFFFF` 再发令 | `Sdio.c:372-380` 真机结论（关闸 → `CMD_INHIBIT` 卡死） |
| SD1 `MSHC_CTRL[16]` + PHY bypass | `Command.c:31-56`；`sdhci-cv181x.c` |
| 不进 1.8 V | `Card.c:28`；厂商 `SDHCI_QUIRK2_NO_1_8_V` |
| HS：EHS → 时钟 → `HOST_CONTROL` High Speed | `Sdio.c:517-554` |
| 4-bit：先 CCCR `0x07` 再主机宽度 | `Sdio.c:559-576` |
| CMD53 FIFO `OP_CODE=0`（固定地址） | `Io.c` + `SdioFifoTransfer` |
| 50 MHz 目标 vs 375 MHz 分频实际值分开记 | `Private.h:20-26`、`Host->ActualClockHertz` |
| GPIOA15 先低后高的 POR 脉冲 | `Power.c` 头注释：接手 FSBL/U-Boot 现场 |
| 默认不驱动 wakeup-gpio（PE7） | 与厂商零调用一致；仅 `wifiprobe wake` 实验 |

---

## 8 · 建议修序（仍不动代码，只排序）

| 优先 | 项 | 为何 |
| --- | --- | --- |
| 1 | **L1** Hex 走同一 sink | 否则真机/shell 排障缺证据 |
| 2 | **A1/A2** 拿掉或隔离 FIFO 回环，失败勿假绿 | 否则 S3「成功」不可信，还可能脏 FIFO |
| 3 | **S1** 拆 `Sdio.c` ≤300 | 再堆 S4 固件下载会不可读 |
| 4 | **P1/P2** R5 提前查、字节模式寄存器 | 进 S4 数据相前补齐 |
| 5 | **B2** 探针勿堵 SD0 Probe | 启动体验 |
| 6 | A3/B1/P3/P4 | 随下一刀顺手 |

**进 S4 的硬门禁（照抄时序表）：S3 串口必须出现 CIS manfid 目标行；不要用 FIFO rw ok 替代。**

---

## 9 · 文件清单（评审覆盖）

| 文件 | 行数约 | 角色 |
| --- | --- | --- |
| `Sdhci.h` | 157 | 门面 / 寄存器 / Host |
| `Sdhci/Private.h` | 117 | 内部 API / 板级常量 |
| `Sdhci/Sdhci.c` | 125 | 驱动注册、SD0 块后端、顺带调 WiFi |
| `Sdhci/Sdio.c` | **663** | SDIO 枚举 + 自检（主战场） |
| `Sdhci/Io.c` | 132 | CMD52/53 |
| `Sdhci/Command.c` | 164 | 复位 / PHY / 时钟 / 发令 |
| `Sdhci/Transfer.c` | 155 | PIO + SD 单块 |
| `Sdhci/Card.c` | 151 | SD0 卡初始化 |
| `Sdhci/Power.c` | 242 | 时钟门控 / 上电 / CD / pad peek |
| `Sdhci/Utility.c` | 152 | MMIO / 延时 / 日志 / wait |

---

*文档日期：2026-10-30。对应对话评审收口；代码变更须另开刀（JX/KG），本文件不代替 ★。*
