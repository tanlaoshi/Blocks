# 04 · PI 3B WiFi 交接给 Cursor

> 用途：把 PI 3B 板载 WiFi（BCM43430 / CYW43438）的开发交给 Cursor 时，直接把本文件给它。
> 它没参与 DUO 那轮的实现，所以这里把**能踩的坑**和**不能碰的边界**一次写清。
>
> 权威路线见 [`Archive/路线图1030.md`](../Archive/路线图1030.md) §九（7 步 + 验收字符串）；
> 本文只补充「交接口径」：边界、资料、前 3 刀具体化、回报格式。

---

## 0 · 一句话任务

**让 PI 3B 的板载 WiFi 在 Blocks 里跑起来**：SDIO 枚举 → backplane 访问 → ALP/HT 时钟 →
固件下载 → SDPCM → scan/assoc → DHCP。

**先只做 P1–P3**（到 HT 时钟为止），这是上次失败的那一段；P4 之后再谈。

---

## 1 · 硬边界（先看这条，避免和 DUO 撞车）

| 允许改 | 禁止改 |
|---|---|
| `Kernel/HAL/RiscV/Board/raspberry-pi-3b/**`（新建） | `Kernel/HAL/RiscV/Board/milk-v-duo-s/**`（DUO 正在改） |
| PI 专属板级代码、PI 的 shell 命令 | `Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/*` |
| 新增独立文件 | `Kernel/Library/`、`Kernel/Services/` 内被多板共享的模块 |

**为什么不许抽公共 `Sdhci` 层**：DUO 侧正在 `Sdio.c` 上做固件下载（B2），**此时抽公共层会两边同时改同一批文件**。
等 DUO 收口后再做 §9.5 的抽取，那时 PI 已经有可用实现，抽取风险更低。

**做法**：PI 侧先复制一份 `Sdhci` 到自己的板目录（`Board/raspberry-pi-3b/Sdhci/`），
在文件头写一行：

```c
/* 暂时复制自 milk-v-duo-s/Sdhci；等 DUO 收口后按 1030 §9.5 抽公共层 */
```

**复制不是技术债，是没有更便宜的办法时的正确选择**——但要留下这行注释，否则半年后没人知道为什么有两份。

---

## 2 · 资料（已全部在本地，不需要再下载）

| 类别 | 路径 | 用途 |
|---|---|---|
| 厂商驱动（SDIO 层） | `Source/PI/wifi/driver/` `bcmsdh.c` `sdio.c` `chip.c` `core.c` `firmware.c` | **主对照物**：枚举、backplane、固件下载 |
| 参考实现（更干净） | `Source/PI/wifi/reference/cyw43_ll.c` / `.h` | Pico W 的裸机驱动，**最适合照抄结构**（无 OS 依赖） |
| 固件 | `Source/PI/wifi/firmware/brcmfmac43430-sdio.bin`（约 400 KB） | 要下载进芯片的 blob |
| NVRAM | `Source/PI/wifi/firmware/brcmfmac43430-sdio.txt` / `...raspberrypi,3-model-b.txt`（后者优先） | 板级校准参数，**与固件版本必须配对** |
| 数据手册 | `Source/PI/wifi/CYW43438.PDF` | backplane 寄存器、SDIO 函数定义 |
| 板上实况 | `Source/PI/wifi/board/brcmf-env.txt`、`pi-config.txt`、`brcmf-debug.log`、`Info.txt` | 能跑起来那台机器的真实配置（比文档准） |

**优先级**：`cyw43_ll.c`（结构） > 厂商 `driver/`（细节） > PDF（语义） > 板上日志（事实）。

---

## 3 · 上次为什么失败（必读，别重复）

上一次已经把固件全灌进去了，双核也出了复位，但卡在：

```
csr=0x50
```

`0x50 = ALP_AVAIL(0x40) | HT_AVAIL_REQ(0x10)`，即：

> **ALP 时钟已就绪，HT 请求已发出，但 HT 一直没批准 → 芯片没有真正开始执行固件。**

**这说明"写完了"不等于"跑起来了"。** 三个可能方向（按可能性排序）：

1. **backplane 窗口没对齐**：`SBSDIO_FUNC1_SBADDRLOW/HIGH` 写错，导致后续读到的 `csr` 本身就是假值；
2. **固件下载地址/长度错**：写到了错误 RAM 段，芯片校验失败后不释放 core；
3. **缺少 ARM core 释放步骤**：厂商在固件尾部写 `SBSDIO_DEVICE_CTL` 或写 chipcommon 的 reset 寄存器，我们可能漏了一步。

**P2 的「写后读回」是分水岭**：如果写入 `0x1000A/0x1000B` 后读不回同值，后面所有判断都不成立。

---

## 4 · 前 3 刀（每刀一个判据，不到判据不往下走）

### P1 · SDIO 枚举

**做**：SD1 初始化（PI 3B 的 WiFi SDIO 控制器基址/时钟与 DUO 不同，按 `pi-config.txt` / `dts` 取）、
CMD5 → RCA → CCCR → 4-bit → HS。

**判据（串口逐字）**：

```
Pi: SD1 init ok clock=50MHz width=4bit
Pi: CIS manfid=02d0 device=a9a6
```

`02d0` = Broadcom，`a9a6` = BCM43430/1 的 SDIO 设备号。**这两个值不对就是枚举没通**，别往下做。

### P2 · backplane 访问

**做**：写 `SBSDIO_FUNC1_SBADDRLOW(0x1000A)` / `SBADDRHIGH(0x1000B)` 打开窗口，
经 F1 读写 backplane 寄存器；读 chipcommon 的 `chipid`。

**判据**：

```
Pi: backplane wr/rd ok addr=... wrote=.. read=..
Pi: chipcommon chipid=43430 rev=..
```

**必须做「写后读回同值」**，这是唯一能证明窗口对齐的方法。

### P3 · ALP / HT 时钟

**做**：`SBSDIO_ALP_AVAIL_REQ(0x08)` → 等 `ALP_AVAIL(0x40)`；
`HT_AVAIL_REQ(0x10)` → 等 `HT_AVAIL(0x80)`。超时要打**每一轮的 `csr` 原始值**。

**判据**：

```
Pi: ALP ok csr=40
Pi: HT ok csr=c0
```

**并且必须回答**：上次的 `0x50` 是怎么造成的（是窗口错、还是漏了释放 core、还是超时太短）。

---

## 5 · 报告格式（每次交付照这个写）

```
## 本轮做了什么
## 真机日志（原文，不要摘要）
## 判据是否达成（逐条 yes/no）
## 失败时的假设与下一次实验（谁 vs 谁，日志怎么区分）
## 改了哪些文件（路径 + 行数）
## 没做的 / 存疑的
```

**日志一律贴原文**，不要"整理"——摘要会把关键位丢掉。

---

## 6 · 协作规则

| 规则 | 说明 |
|---|---|
| **单写者** | 同一时间只有一个人改 PI 目录；DUO 目录你完全别碰 |
| **不 commit、不 push** | 改动留在工作区，由主会话统一提交（保持历史干净） |
| **一次一刀** | 每刀独立可编译；不要一边做 P1 一边改 P3 |
| **不猜寄存器** | 每个寄存器地址都要能指到 `cyw43_ll.c` / 厂商 `driver/` / PDF 的具体位置 |
| **失败要能区分** | 每次加日志都对"两个假设"下注，别打一堆看不懂的 hex |

---

## 7 · 与 DUO 的复用关系（将来，不是现在）

DUO 侧已经跑通的东西，**将来**可以复用到 PI：

| DUO 已验证 | PI 能不能用 |
|---|---|
| SDHCI 命令/传输/时钟（`Command.c`/`Transfer.c`） | ✅ 同 IP，换基址即可 |
| CMD52/53 参数位、R5 拆位 | ✅ 直接复用 |
| **读路径自证**（CMD53 vs CMD52 交叉验证） | ✅ **强烈建议照抄**，这是最便宜的正确性证明 |
| CIS 解析 | ✅ 结构相同（MANFID 都在 Common CIS `0x001000`） |
| AIC8800 的 vendor init / 消息协议 | ❌ 完全不同，别照搬 |
| 流控 credit / 512 补齐 | ❌ 那是 AIC 的私有协议 |

**PI 的固件下载由我们自己发数据**（brcmfmac 的 `download_firmware`），不是 AIC 那种"发消息让固件自己搬"，
这是两者最本质的差别。
