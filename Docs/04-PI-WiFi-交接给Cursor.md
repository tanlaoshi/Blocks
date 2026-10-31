# 04 · PI 3B WiFi 交接给 Cursor

> 用途：把 PI 3B 板载 WiFi（BCM43430 / CYW43438）交给 Cursor 时，直接给本文件。  
> 权威路线见 [`Archive/路线图1030.md`](../Archive/路线图1030.md) §九；失败细节见  
> [`Archive/过程/排障/Pi-WiFi-43430-失败尝试记录.md`](../Archive/过程/排障/Pi-WiFi-43430-失败尝试记录.md)。  
> 本文只定**交接口径**：与 DUO 并行边界、续作现有代码（勿另起炉灶）、资料缺口、下一刀、回报格式。

---

## 0 · 一句话任务

**在现有 Pi 板包上**把板载 WiFi 跑通：背板窗口可信 → SRAM 可写 → ALP/HT → 固件真正跑起来 →（之后）SDPCM / scan / DHCP。

**先只做「解开 SRAM / HT」这一段**（失败记录暂存点）；P4 固件下载验证通过之前，不要碰 scan/assoc。

---

## 1 · 硬边界（与 DUO 并行）

| 允许改 | 禁止改 |
|---|---|
| `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/**` | `Kernel/HAL/RiscV/Board/milk-v-duo-s/**`（DUO 正在改） |
| 同板 `rpi-3b/Sdhci/**`（**仅** WiFi 路径必需时） | 新建 `HAL/RiscV/Board/raspberry-pi-3b/`（**错架构，仓里没有这个板**） |
| `Archive/过程/排障/Pi-WiFi-*`、本文件、`Source/PI/README.md` | 从 DUO 复制一份 `Sdhci` 到 Pi（Arasan 栈已在板包里） |
| | `Kernel/Library/**`、`Kernel/Services/**` 里多板共享模块 |
| | **`Kernel/Services/Shell/Commands/Wifi.c`**（已挂 DUO 的 `SdhciWifiProbe` / `SdhciWifiFw` weak；并行改必撞） |

**为什么现在不许抽公共 `Sdhci`（暂缓 1030 §9.5 / PI-1）**：DUO 还在收口固件下载；抽公共层 = 两边同时改同一批文件。等 DUO 回归绿再抽。并行期用**目录隔离**，不是用「复制 Duo 代码」。

**做法**：续作 **`Arm64/Board/rpi-3b/Wifi/`**（tag 见 `Private.h` 的 `WIFI_BUILD_TAG`）。  
Pi WiFi SDIO 控制器是 **Arasan @ `0x3F300000`**（`rpi-3b/Sdhci*`），µSD 是 SDHOST——勿混。

### 并行时编译

`build arm64 pi` / `BOARD=rpi-3b` **默认** `BUILDDIR=Build/PI`（即 `Kernel/Build/PI/`），
与 Duo/riscv 用的 `Kernel/Build/` 错开，避免互清 ELF。  
`ck-pi-sd` / `prepare-pi-sd` 默认吃 `Build/PI/HAL/Arm64/Kernel.elf`。  
仍可用 `BUILDDIR=…` 显式覆盖。

---

## 2 · 现有代码状态（必读，勿推倒重来）

路径：`Kernel/HAL/Arm64/Board/rpi-3b/Wifi/`（失败记录暂存 **tag `0x122`**）。

| 已验证 | 未通 / 未验证 |
|---|---|
| SDIO 枚举、F1、块长、背板窗读写 | 三个 SRAM 别名 CMD53：**接命令、进传输、零字节**（R5 无错位） |
| EROM、五核基址、SOCRAM 尺寸/银行 | 固件真正写入 SRAM（路径有，阵列不可达） |
| ALP / `buscoreprep` 等价、D11/SOCRAM 复位顺序 | 拉 ARM 后 HT / F2 / SDPCM |

**当前卡点不是「还没枚举」**，而是：**`0x18000000` 寄存器区正常，SRAM 别名 `0` / `0x10000000` / `0x1e000000` 写不进**。  
串口上的 `csr=0x50`（`ALP_AVAIL|HT_AVAIL_REQ`）是固件没跑起来的**后果**；先把 SRAM 通路坐实，再谈 HT 批准。

失败记录「下一步」（一次只动一项）：

1. WL_ON 后等待 0.5 s → **3 s**（对照 Linux 加电到驱动接手）；
2. 与 Linux `mmc1 ios` 对齐时钟/时序（见 `Source/PI/wifi/board/pi-mmc1-ios.txt`：**50 MHz / 4-bit / sd high-speed / 3.3 V**）；旧代码曾用 25 MHz default——改这一项时打清 `WIFI_BUILD_TAG`；
3. 仍不通 → Linux 侧最小探针模块（失败记录里已写，未再跑）。

---

## 3 · 资料（本地有；缺口写明）

| 类别 | 路径 | 用途 |
|---|---|---|
| 本仓实现 | `Kernel/HAL/Arm64/Board/rpi-3b/Wifi/` | **续作主战场** |
| 失败记录 | `Archive/过程/排障/Pi-WiFi-43430-失败尝试记录.md` | 已否掉的假设，禁止重踩 |
| 厂商驱动 | `Source/PI/wifi/driver/`（`bcmsdh`/`sdio`/`chip`/`core`/`firmware`） | 对照流程与常量 |
| 裸机对照 | `Source/PI/wifi/reference/cyw43_ll.c` / `.h` | ALP/HT/backplane 结构 |
| 固件 | `Source/PI/wifi/firmware/`（`brcmfmac43430-sdio.bin`、`cyfmac43430-sdio.bin`） | 注意仓内可能有多份、**md5 须与板上工作副本核对** |
| NVRAM | 同目录；**优先** `...raspberrypi,3-model-b.txt` | 与固件版本配对 |
| PDF | `Source/PI/wifi/CYW43438.PDF` | 语义；ALP/HT 细节仍以 `cyw43_ll` 为准 |
| 板级 | `Source/PI/wifi/board/`（`pi-dt.dts`、`pi-mmc1-ios.txt`、`pi-wifi.txt`、`Info.txt`…） | 真机事实 |

**优先级**：失败记录 + 本仓 `Wifi/` > `cyw43_ll` > 厂商 `driver/` > PDF > 板上日志。

**缺口（不挡开刀，挡「一次解释清 0x50」）**：

- `board/brcmf-debug.log` 可能仍空；`brcmf.log` 多为驱动起来后的 runtime，**不是**冷启动灌片 TRACE。需要时按 `board/Info.txt` 重抓。
- 仓内 bin 与板上 `md5` 可能不一致；缺 A0 blob 时不要用「换固件」当第一假设。

---

## 4 · 前几刀（接在现有栈上；不到判据不往下）

判据字符串可与旧日志并存；**以 `Boot: Pi WIFI` / `WIFI_BUILD_TAG` 为准**，不必另起一套 `Pi: SD1…` 新树。

### 刀 A + 刀 B（已并进同一 tag，一次烧）

无多余串口时：靠**屏上 Boot 字**或落盘日志（`BOOT.LOG` / ring），不必每刀插 TTL。

| 刀 | 做了什么 | 串口/屏上应见 |
|---|---|---|
| A | 50 MHz + HISPD（EHS 失败则 25 MHz） | `clock=50MHz width=4bit HS` |
| B | WL_ON 后等待 0.5 s → **3 s** | `WL_ON cycle wait=3000ms` |
| 共 | `WIFI_BUILD_TAG` | **`0x00000124`** |

**判据**：tag=`124`；再看 `sram base` 三行相对 `0x122` 是否仍全 `no`（原文即可，屏上滚过或日志文件里搜）。

刷卡：卡挂上后说 **CK**（`ck-pi-sd`），不要 Duo 卡。

### 刀 C · 背板「写后读回」自证（若日志尚不够硬）

**做**：对已知可写寄存器（非 SRAM 别名）写图案再读回；失败则停，禁止猜 HT。

**判据**：

```
Boot: Pi WIFI backplane wr/rd ok …
Boot: Pi WIFI chipid=…（与 EROM / 0xa9a6 叙事一致）
```

### 关于 P1 枚举

枚举/CIS/`02d0:a9a6` **已在旧栈验证过**。不要为「跑通 P1 验收句」重建控制器。若回归坏了再修枚举；默认假设 P1 绿。

### HT（`csr=0x50`）

**在 SRAM 可写之前，不把「等到 HT_AVAIL」当独立成功刀。**  
HT 超时必须打每轮 `csr` 原文，并回答：窗口假值 / 固件没落地 / 漏释放 core——用日志区分，禁止三选一瞎改。

---

## 5 · 报告格式（每次交付）

```
## 本轮做了什么
## WIFI_BUILD_TAG / 改了哪些文件（路径 + 大约行数）
## 真机日志（原文，不要摘要）
## 判据是否达成（逐条 yes/no）
## 失败时的假设与下一次实验（谁 vs 谁，日志怎么区分）
## 没做的 / 存疑的
## 确认未碰：milk-v-duo-s、Shell/Commands/Wifi.c、Library、抽公共 Sdhci
```

**日志一律贴原文。**

---

## 6 · 协作规则

| 规则 | 说明 |
|---|---|
| **单写者（Pi 目录）** | 同时只一人改 `rpi-3b/Wifi`（及必要的同板 `Sdhci`） |
| **DUO 只读** | 可对照 `milk-v-duo-s` 的 CMD52/53 / 读路径自证思路，**禁止改** |
| **不 commit / 不 push** | 除非用户明示 TG / TS |
| **一次一刀** | 独立可编译；勿同时改等待时间 + 时钟 + 复位顺序 |
| **不猜寄存器** | 地址须能指到 `cyw43_ll` / `driver/` / PDF / 失败记录已解 EROM |
| **失败要能区分** | 每条新日志对应两个互斥假设 |

---

## 7 · 与 DUO 的复用（将来，不是现在）

| DUO 已验证 | 现在能不能用 |
|---|---|
| CMD52/53 参数位、R5 拆位、读路径自证思路 | ✅ **照抄思路**到 Pi 日志；不要拷 Duo 文件过来 |
| CIS 解析结构 | ✅ 已有；MANFID 目标是 `02d0:a9a6` |
| SDHCI 命令/PIO 实现 | ⚠️ Pi 用 **本板 Arasan `Sdhci`**，不是 Duo 的 cv181x 副本 |
| AIC vendor init / 流控 / 512 补齐消息 | ❌ 私有协议，禁止照搬 |
| 抽公共 `Sdhci` 层 | ❌ **DUO 收口 + 回归后再做**（1030 §9.5） |

**本质差别**：Pi 固件是主机经 backplane **自己写 RAM**（brcmfmac `download_firmware`）；Duo/AIC 是消息让芯片侧搬。并行开发时两边协议不要混。
