# Milk-V Duo S 笔记（PR-B3）

不要把秘密或专有固件 blob 提交进库。

**真机排障编年**（镜像 / 串口 / bootelf / `epc=0`）：  
[`Archive/过程/排障/MilkV-DuoS真机启动排障历程.md`](../../../../Archive/过程/排障/MilkV-DuoS真机启动排障历程.md)

## 硬件

- SoC：SG2000（CV181x）；本包目标核：**RISC-V C906**
- DRAM：约 512MiB @ `0x80000000`（U-Boot 常报 ~510 MiB）
- UART：USB-UART ↔ 板载调试口（常见 Pin：GND=6、TX=8、RX=10，以丝印为准）
- 启动介质：microSD（厂商镜像）或 TFTP

## 厂商 U-Boot

- 预装即可；**不改**源码，只改环境变量 / 卡上脚本 / 手工 `load`
- 常见：`kernel_addr_r=0x80200000`，`fdt_addr_r=0x81200000`
- 加载优先：`bootelf -p`（ELF）或 `go`（`.bin`）；`booti` 需 Linux Image 头（本柱不做）

## 启动 / 刷卡（duo-boot / autoboot / blocks-elf）

- `bootdelay=1` + 部分镜像 `ENV nowhere` → **`saveenv` 常存不住**。  
- 真机：`bootcmd=run sdboot` → **`boot.sd`**（不扫 `boot.scr`）。  
- **duo-fatload**：FIT `type=script` 真机失败（bootm 仍要 RISC-V Kernel Image）；上电仍 **embed FIT**；抢壳 `boot.scr` 才是 fatload BLOCKS。  
- **应用卷**：`source env.sh && ck-duo-sd` → **`Kernel.elf`→LABEL=BLOCKS**。  
- **禁止** `ck-pi-sd`。详见 [`scripts/README.md`](scripts/README.md)。

## USB Host（duo-usb-kbd）

- DWC2 @ **`0x04340000`**；PHY/ID/VBUS：`TOP 0x03000048`（host：EXTVBUS|VBUS_PWR|ID_OVR，清 ID_VALUE）；ECO RX flush @ `0x030000B4`。  
- Type-A **VBUS_EN** = **XGPIOB[5]**（pinmux `USB_VBUS_EN`→3）；`HalUsbInitialize` → `BoardUsbHostControllerProbe` → `DriverInputAttach`。  
- 根口可直连 boot 键；hub 路径同 Pi SPLIT。串口壳 `HalInputPoll`+`HalKeyboardDequeue`。  
- 手测：键须插 **Type-A**（冷启前插好更稳）；看 `Boot: Duo USB` / `DWC2 … kbd`。

## 以太网 MAC（duo-net · net-1）

- DTB：`ethernet@4070000` · `compatible = "cvitek,ethernet"` · `phy-mode = "rmii"`（`scripts/duo.dtb`）。  
- MMIO：**`0x04070000`**，窗 `0x10000`。  
- 身份：mainline `snps,dwmac-3.70a` → **`GMAC_VERSION @ +0x20`**；亦兼容 EQOS `@+0x110`（U-Boot `dwc_eth_qos`）。串口打 `ver20`/`ver110`。  
- 时钟：clkgen **`0x03002000`** · EN_0 **bit25/26**；DIV `@0x08C`（en bit8、div[19:16]）；BYP0 bit9。  
- 复位：TOP **`0x03003000` bit12** 仅 **OR 释放**（不拉低脉冲）。  
- 板内 PHY：`0x03009000` — **net-2**。  
- **真机 net-1（2026-10-26）**：`ver20=00001037` · `ver110=0` · `mac0=00008000` → **3.70a@0x20**。  
- **实现文件**：`Ethernet.c` / `Ethernet.h`（`BoardEthernetProbe`；Naming 全词，三架构同门面）；块设备 `Sdhci.c`（`BoardSdhciRegister`）。  
- **net-2**：MDIO `MII_ADDR/DATA` @`+0x10/+0x14`（shift 11/6，CSR=4）。EPHY：`cv181x_ephy_id_init`（写 ID `0043:5649`）+ Linux `cvitek` `config_init`（ANA 表、`CTL=0x090e` 启 AN、BMCR FD）；MDIO 扫 addr1；`BMCR` 重启 AN；轮询 `BMSR`。串口 `phy=` / `id=` / `bmcr=` / `adv=` / `lpa=` / `link=`。无 link **不**称 Ready。  
- **真机 net-2（2026-10-26）**：`phy=1 id=00435649` · `bmcr=3100` · `bmsr=79ed` · `adv=01e1` · `lpa=cde1` · `aneg=1` · **`link=up`**。  
- **net-3**：DMA **normal** 描述符（`dmafeat=420d5925` → ENHDESSEL=0）；MAC `LM` 环回；`HalDcacheCleanInvalidateRange`。测完关 LM、停 DMA。仍 **`BOARD_HAS_NET=0`**。  
- **真机 net-3（2026-10-26）**：`enh=0 normal` · **`DMA tx=ok`** · **`rx=ok len=00000044`**（64+FCS）。  
- **net-4**：`BOARD_HAS_NET=1`；`BoardEthernetRegister` → Probe/Bind → `DriverNetAttach`；LM 自检后关 LM **常驻** DMA；`HalNetInitialize` = `DriverProbeClass(NET)`（**不** `VirtioNetRegister`）。**ConsoleOnly 无 Worker**：`gModulesVirt` 含 Network，模块里直接 `HalNetInitialize`（桌面仍 defer 到 Worker）。串口 `Duo ETH probe`…`DMA wire` / `ETH bind`；壳 `net` 见 mac/ip/link。  
- **net-5**：ARP 缓存 + ICMP echo（对齐 Virtio Arp/Protocol）；默认 IP `HAL_NET_IP_DEFAULT`（`10.0.2.15`）— 局域网请 `setip`；壳 `ping <ip>`。  
- **net-6**：`HAL/RiscV/LwIp/`（对表 X64）；`LWIP=1`；`BoardEthernetSendFrame` + RX→`LwIpNetifInput`；串口壳 `LwIpDhcpStep`（无 Worker）。手测：`lwip on` → `net dhcp` → `net`/`ping`。  
- 刷卡前 `.toy_board=milk-v-duo-s`。

## µSD（duo-block）

- 控制器：`sdhci0` / `sophgo,cv1800b-dwcmshc` @ **`0x04310000`**（上游 `cv18xx.dtsi`）。PIO 读 CMD17 / 写 CMD24。  
- 复位后厂商区：Linux `cv18xx_sdhci_reset`（latency 1T、PHY TX bypass、TX/RX 时钟反相）。SD 槽**不**置 `EMMC_FUNC_EN`。  
- 基钟不用 CAPABILITIES（该字段坏）：按 U-Boot `cv1800b_sdhci.c` 的 **375 MHz** 做分频。识别 400 kHz，传输 25 MHz，不进 1.8V / 不调相。  
- `rdtime` 按 `cv18xx.dtsi` **25 MHz**。  
- 不发擦除。FAT 有 `fip.bin`/`boot.sd` 时卷名常叫 ESP——**勿故意写坏 boot**；默认卷 BLOCKS。  
- `exec HELLO.ELF` / `Kernel.elf`：放 **LABEL=BLOCKS**（与 X64/Pi 同）。  
- 卡检测位为 0 则放弃（`Boot: SD no card`）。U-Boot 刚 `fatload` 过却报无卡，把 `pst=` 记下来再改。

## WiFi / SDIO（duo-wifi · PR-30 S1-S3）

板载 AIC8800D80 挂在 **SD1**（`0x04320000`，wifi-sd），SD0（`0x04310000`）仍是启动卡槽。
`Sdhci` 现在是两实例：`SdhciHostAt(0/1)`，块设备只暴露 SD0。

- 上电脚：`GPIOA15`（焊盘 SPK_EN，TRM 写明 FMUX 默认就是 `XGPIOA[15]`，不用配 pinmux）→ 拉高后等 50 ms。
- 卡检测：wifi-sd 是 `non-removable`、无 CD 脚。写 TOP `0x03000294` bit8（override）/ bit9（software）＝假卡。
  Linux 在 non-removable 上压根不读 `PRESENT_STATE[16:17]`（`sdhci.c:2438-2466`），所以
  `SD1: present=… CARD_INSERTED=… CARD_STABLE=…` 只打印、不门禁。
- SD1 专用 PHY 位：`MSHC_CTRL[16]`（厂商 `sdhci-cv181x.c:859-862`；TRM 名字是 `timer_clk_sel`）。
- 时钟：基础 375 MHz，请求 50 MHz → 实际 **46.875 MHz**（`SDCLK=8`，精确 50 MHz 取不到）。
  真机串口会打 `WiFi: SD1 clk actual=46875kHz`。
- HS：CCCR `0x13` 置 `EHS`，同时 `HOST_CONTROL[2]=1`（Linux `sdhci.c:2338-2355`）。
- 4-bit：先写卡 CCCR `0x07=2`，再置 `HOST_CONTROL[1]`。
- SDIO 命令：CMD0/5/3/7 + CMD52/CMD53，PIO 按 512 B 块搬（`Sdio.c` / `Io.c`）。
- 1536 B 固定地址 FIFO 自检写在 `SdhciWifiInitialize()` 末尾：只证明「多块 + `OP_CODE=0` 能走完」，
  **不**证明数据正确（`0x10` 是发往固件的 TX FIFO，`0x0F` 是固件回的 RX FIFO，没有回环通路）。
  真机上 diff 属预期，不致命。

期望串口（首次真机应重点看这两行 + 前面的 CMD5/CCCR 行）：

```text
WiFi: SD1 init ok clock=50MHz width=4bit
WiFi: CIS manfid=c8a1 device=0082
```

排障顺序：`WiFi: TOP 0x294=…` 两位没置上 → 覆盖无效；`SD1 CMD5 no response` → 供电/pad/时钟；
`CMD5 timeout` → 电压窗口；`CCCR read fail` 带 `r5=` → CMD52 参数或 R5 错误位。

### 诊断（2026-10-30 真机"一行都没有"之后加）

- `SdhciMark*` 不再走 `HalSerialBootMark`（SLOG_BOOT 通道，编译期 `SERIAL_BOOT=0` 就静默），
  改成 `HalSerialWriteShell` **直写 UART**；shell 里重跑时用 `SdhciWifiMarkSink()` 换成
  `gConsole.Write`，输出才和提示符对齐。
- 三个定位点：
  `Boot: duo-sdhci build=<日期 时间>`（`BoardSdhciRegister`，证明卡上是这次编的内核）→
  `Boot: sdhci probe enter`（`SdhciDriverProbe`，证明 BLOCK 探测走到我们）→
  `WiFi: probe start build=…`（`SdhciWifiInitialize` 入口，无条件打）。
- 每条失败路径都打 `WiFi: FAIL at <阶段>` 并带上现场寄存器（`SRSI`/`clock`/`pwr`/`int`/`pst`/`r5`），
  不再有静默 `return -1`。
- shell：`wifiprobe`（或 `wifi probe`）清掉"只试一次"的状态重跑 SD1，不用重启板子。

### 真机 CMD5 无响应（2026-10-30 第二次上板）→ 时序对齐厂商

现场：`version=0005`、`clk=d547`（400 kHz，bit0/1/2 全置）、`pwr=0f`、`0x294=0300`，但 CMD5 后
`INT=0`、`PRESENT_STATE` 不变。查厂商源码的结论（都核对到行）：

| 问题 | 厂商怎么做的 | 出处 |
|---|---|---|
| 上电延时 | `gpio_direction_output(pwr,1); mdelay(50);` —— **就是 50 ms** | `aic8800_bsp/aicsdio.c:499-520` |
| 复位脉冲 | **没有**（不先拉低） | 同上 |
| wakeup-gpio(PE7) | 整树**零调用**，只 `cvi_get_wifi_wakeup_gpio()` export 出来没人用 → 不是上电使能 | `soc/cvitek/wifi_pin/cvi_wifi_pin.c:41-54` |
| 上电后等多久 | `down_timeout(&aic_chipup_sem, 2000ms)`：给芯片 **2 s** 反应窗口 | `aicsdio.c:534` |
| 谁去发 CMD5 | `cvi_sdio_rescan()` → `mmc_detect_change()` → 标准 `mmc_rescan` | `sdhci-cv181x.c:548-562` |
| SD1 时钟门控 | CCF 里 `clk_axi4_sd1/clk_sd1/clk_100k_sd1` = `CLK_EN_0` bit21/22/23，带 `CLK_IS_CRITICAL`；CRG 基址 `0x03002000` | `clk-cv181x.c:551-575`、`cv181x_base.dtsi:26-28` |

所以我们少了三样（都不在"延时"上）：**① 时钟门控没人开**（Linux 有 CCF，我们没有）、
**② 模块可能被 FSBL/U-Boot 留在半初始化态**（`0x294` 在我们写之前就是 `0x300` 即为证），
**③ 判死太早**（100 ms vs 厂商 2 s）。对应改动：

1. `DuoWifiClockGate()`：读 `CLK_EN_0`，缺 bit21/22/23 就补上并回读打印。
2. 顺序改成**先 400 kHz 时钟、再上电**（厂商也是 host 先跑起来，`aicbsp` 之后才上电）。
3. `DuoWifiPowerOn()`：保留厂商 50 ms，前面加 **低 100 ms → 高** 的断电脉冲做一次干净 POR。
4. CMD5 从"发一次"改成 **12 次 / ≈1.8 s 窗口**，每次打 `CMD5 #n st= pst=`；
   注意 `SdhciCommandSend` 失败时会做 CMD/DATA 复位把 `INT_STATUS` 清掉，
   所以要用 `Host->LastStatus`（复位前抓的），别现场再读。
5. `DuoWifiCardDetect()` 做**极性自测**：`sw=1` 读一次、`sw=0` 读一次 PRESENT_STATE[16]，
   谁让它变 1 就留谁；都不行就恢复原值（不门禁）。
6. `DuoWifiWakePeek()`：**只读** GPIOE（`0x05021000`，port E 不在 `0x0302_x000` 那一排）的 DDR/DR，
   不驱动 PE7。

### 08:24 真机 → 根因：`INT_ENABLE = 0`（2026-10-30 第三次上板）

现场：`CLK_EN_0=ffffffff`（门控本来就全开）、`GPIOA15` 高低都对、CD 两种极性都不进
`PRESENT_STATE[16]`；12 次 CMD5 全是 `st=00000000 pst=03f20001`，最后 `clk=d543`。

判读：`pst` bit0 = `CMD_INHIBIT` 恒 1、`INT_STATUS` 恒 0、时钟使能位(bit2)被硬件清掉 ——
**命令进去了、卡在 FSM 里出不来**。原因在 `Sdio.c` 自己：

```
SD0：SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0xFFFFFFFFu)   ← Card.c:52
SD1：SdhciControllerBringUp(Host, IDENTIFY_HERTZ, 0)              ← 之前这里写的是 0
```

SDHCI 的 Interrupt Status **Enable** 是状态位的总闸：闸关着 → 命令完成/超时都不进
`INT_STATUS` → 轮询永远看不到 → 命令 FSM 等软件 W1C 应答 → `CMD_INHIBIT` 永挂，
后面每次重发在第一道门（inhibit 等待）就返回，所以 12 次一模一样、INT 恒 0。
SD0 一直是 `0xFFFFFFFF`，所以启动盘从没暴露这个问题。**现已改成与 SD0 一致。**

配套诊断（这次一起加的）：

- `WiFi: pre-CMD5 pst= int= err= ien= clk=`：`ien=` 这一栏是判决书 —— 修复前 0，修复后 `ffffffff`；
  `err=` 取 `INT_STATUS>>16`（标准 SDHCI 的 Error Interrupt Status 就在 0x32）。
- `CMD5 blocked by CMD_INHIBIT (cmd line busy)`：进循环前先看 inhibit，卡住就先
  `RESET_ALL + 400k` 拽回来一次，还卡直接判死，不再白跑 12 次。
- 每次失败后再看一次 inhibit，卡住立刻跳出重试。
- `WiFi: pads(0=SD1 func) d3= d2= d1= d0= cmd= clk=`：只读 U-Boot `PINMUX_SDIO1/SD1_SDIO_PAD`
  那六个焊盘（`FMUX 0x03001000` + `0xD0/0xD4/0xD8/0xDC/0xE0/0xE4`，功能号 0 = SD1）。全 0 才说明
  焊盘切对了；不是 0 就说明 SD1 连一根线都没出去。
- `wifiprobe wake`：一次性实验，把 PE7 驱动成输出拉高（`DuoWifiWakeDrive`）。
  默认路径不动它 —— 厂商 `cvi_get_wifi_wakeup_gpio()` 零调用，原厂 Linux 不碰也能起。

### 09:07 真机 → CIS 判死的根因：在 fn1 的 CIS 里找 MANFID

`INT_ENABLE` 修好后一次就枚举成功（`CMD5 ok after #1`、`r4=20ffff00`、4-bit/50 MHz 全通），
只剩 CIS：`cis ptr=001011 manfid=0000`。

**0x1011 不是坏指针**，它是 fn1（function 自己的）CIS 的真地址。错的是我们期待那里有 MANFID：
MANFID(0x20) 属于 **Common CIS**（fn0 的指针在 0x09-0x0B），function CIS 一般只放 FUNCE 之类
（SDIO 规范 §16.4；Linux `drivers/mmc/core/sdio_cis.c:201` `sdio_read_common_cis` 读 fn0，
`sdio_read_func_cis` 读 fnN，两条路分开）。

所以改成：

1. `SdioReadCisPointer(Host, 0, ...)` 读 **fn0 的 0x09-0x0B**（同一函数，`fn*0x100 + 0x09` 对 fn0 就是 0x09），
   用它的 CIS 解析 MANFID/器件号 → `WiFi: module=c8a1 device=0082 (aic8800)`。
2. fn1 的指针照旧解析，但只作函数元组参考（FUNCE 块大小），**不再要求有 MANFID**。
3. 现场一次抓全：`WiFi: fbr1 ` / `cis0 ` / `cis1 ` 各 16 字节 hex（读失败会打 `<read fail +N>`，
   这样"读到全 00"和"根本读不动"能分开）。
4. 兜底 1：`SdioScanCis(0x1000..0x1400)` 扫 `20 04` 元组头，命中就打 `MANFID scan hit`。
5. 兜底 2：还没有就退回 **1-bit / 400 kHz** 再读一次 Common CIS 对照
   （`no MANFID yet, retry 1-bit 400kHz` → `low cis0 …` → `bus restored hctl= clk=`），
   用完恢复 4-bit + 原速。
6. **不再判死**：真找不到 MANFID 就是 `no MANFID tuple, continue without module id`，
   后面照常走 FIFO 自检、`probe end ok`。

### 09:18 真机 → FIFO 读 1536 B 卡死：那不是回环，是消息通道

现场：`FIFO write 1536B ok` 之后 `FIFO read 1536B fail st=00000000 pst=03f20206`
（`0x206` = `DOING_READ`|`DAT_LINE_ACTIVE`|`DATA_INHIBIT`：数据线一直不结束，且**没有**错误位）。

根因不是驱动 bug，是**自检的假设错了**：AIC 的 fn1 FIFO 是**消息通道**，不是回环。
厂商只在芯片有消息时才读（`aicwf_sdio_recv_pkt` → `sdio_readsb`，由 flow-ctrl /
中断驱动，`aicsdio.c:742-751`）。FIFO 里没消息时去读，卡不回数据，DAT 线就永远挂着。
另外 `rd_fifo_addr=0x0F`/`wr_fifo_addr=0x10` 是**固定常量**（`aicsdio.c:1708-1719` 按
chipid 选），不需要先写什么 window/len 寄存器。

改法（`SdioFifoTest`）：

1. **先证读方向**：`CMD53 fn0 读 16 B @0x1000`（Common CIS，卡自己的 ROM，一定有货），
   再和 CMD52 逐字节比对 → `WiFi: DAT read ok (CMD53 fn0 cis 16B matches CMD52)`。
   这条与固件在不在、FIFO 有没有消息**完全无关**，所以它是 DAT0-3 读方向的干净证明。
2. FIFO 写 1536 B 保留（S5 下载固件就靠写方向）。
3. FIFO 读**只探 4 B**（消息头长度），失败即"没有消息"，打
   `WiFi: FIFO read: no message pending (expect before fw download)` 后收尾 —— 不再死等 1536 B。
4. 现场三行：`blksz/blkcnt/tmode/tmo` + **复位之前**的 `int/err/pst`（`Host->Last*`，
   `SdhciPioAbort` 先记再复位，现场读读不到）+ 复位之后的 `now int/pst`，
   错误位细分成 `ADMA/AC12/CMD_END/CMD_IDX/DAT_END/DAT_CRC/DAT_TIMEOUT/CMD_TIMEOUT`。
5. 恢复阶梯（`SdioRecoverBus`）：`RESET_DAT` → 还卡就 `RESET_ALL + 400kHz` + 恢复
   `HOST_CONTROL`/时钟 + CMD52 验活，打印 `bus recovered (RESET_DAT|RESET_ALL)`。

### 09:27 真机 `DAT read mismatch at 01 got=20 want=04` → CMD53 的 OP_CODE 传错

打这行的是 `SdioDatReadCheck()`（Sdio.c）。根因是**参数**，不是读路径：

```
SdhciIoRwExtended(Host, 0, 0x1000u, 0, /*Increment=*/0, 0, 16u, Buf)
                                      ^ OP_CODE=0 = 固定地址
```

固定地址的多字节读，卡会把同一个字节重复 N 次 —— 实测 buf = `20 20 20 20 …`，
所以 `buf[1]` 还是 `0x20`，而 CIS 真值是 `0x04`。而且 `20` 恰好是 MANFID 元组头，
"第一字节对上了"特别容易骗人。FIFO（`0x0F`/`0x10`）必须用固定地址，**存储区/CIS 必须自增**。

改法：

1. `SdioDatReadCheck(Host, HostControl, RestoreHertz)`：Increment 改 **1**；
2. 新增 `SdioCmpCisRead()`：把 CMD53 读到的 16 B 和 CMD52 逐字节读的同一地址**并排打出来**
   （`cmd53@001000: …` / `cmd52 ref: …`），不一致就打 `mismatch +N cmd53=xx cmd52=yy`；
3. 不一致先退 **25 MHz / 1-bit** 再对照一次（`bold low cis0`），然后恢复总线；
4. 测试台补齐：假卡的固定地址读改为"同字节重复"，并新增断言
   "发往 CIS 的 CMD53 全部带地址自增" —— 把参数改回 0 会立刻挂 2 条。

### S5b-1：芯片识别（只读）

**厂商"认芯片"不读寄存器**：`aicwf_sdio_chipmatch(vid, did)` 比的是 `func->vendor` /
`func->device`，也就是 CIS 里的 MANFID/PID（`aic8800_bsp/aicsdio.c:239-259`、
`aic8800_fdrv/aicwf_sdio.c:528-552`），表在 `aicwf_sdio.h:75-80`：

| 型号 | vendor | device |
|---|---|---|
| AIC8801 | `0x5449` | `0x0145` |
| AIC8800DC | `0xc8a1` | `0xc08d` |
| **AIC8800D80** | `0xc8a1` | **`0x0082`** |

`chip_rev` 那个 `0x40500000` **不是寄存器读**，是 AIC 的 dbg-mem-read **消息**
（`aic_bsp_driver.c:1876-1880`、`:1924-1929`），而且只用来在 `fw_8800d80_u01/u02` 之间选；
SDK 里 D80 只发了 u02 一套（`fmacfw_8800d80_u02.bin` 等），所以 S5b 直接锁 u02。

新增两行只读判据：

- `WiFi: chip id=aic8800d80 (manfid=c8a1 pid=0082)` + `WiFi: fw profile=8800d80_u02 …`
- `WiFi: fn1 regs ien= ipd= itd= fc1= misc= len= lenh= bme= mctl= fc2= clk=`
  —— fn1 的 V3 寄存器快照（`aicsdio.h:35-48` 的 00..0A），不写一个字节。

身份不明时只打 `WiFi: chip id=unknown …` + 厂商表，**不拦 S3 枚举**；闸放在下载命令里。

### S5b-2：读取层（A 方案）+ 消息通道 + adid/patch_table

固件不嵌内核，`WifiFw.c` 用 `FileSystemFileStat` + `FileSystemReadFileAt` 从卡上读：

```
/Assets/Fw/aic8800/fw_adid.bin
/Assets/Fw/aic8800/fw_patch_table.bin
```

判据行：`WiFi: fw read /Assets/Fw/aic8800/fw_adid.bin 1708B ok`（字节数必须等于文件大小）。

**AIC 消息协议**（厂商 `aic8800_bsp`）：

| 项 | 值 | 出处 |
|---|---|---|
| 帧头 | `len(12bit)` / type `0x11` / crc8(poly 0x107) | `aic_bsp_driver.c:258-283`、`aicsdio.c:2004-2027` |
| 帧头后 | 4 B dummy + 8 B lmac_msg 头（id/dest/src/param_len） | `aic_bsp_driver.c:268-283` |
| 请求写 | fn1 `0x10`（FIFO，固定地址） | `aicsdio.c:742-751` |
| 应答读 | **fn2** `0x0F`（Linux `func_msg`） | `aicsdio.c:753-780`、`aicsdio_txrxif.c:213-215` |
| 有没有消息 | fn1 `0x04` bit7 `SDIO_OTHER_INTERRUPT` | `aicsdio.c:1458,1506-1514` |
| 消息 id | `DBG_MEM_READ_REQ=0x400`/`CFM=0x401`、`DBG_MEM_BLOCK_WRITE_REQ=0x40B`/`CFM=0x40C` | `aic_bsp_driver.h:181-215` |
| adid/patch 落点 | `0x00201940` / `0x0020b43c`（u02） | `aic_bsp_driver.h:358-359` |
| 分块 | 1 KB 一块 | `aic_bsp_driver.c:1256-1266` |

**fn2 不是废的**：厂商把消息/确认通道放在 fn2（`func_msg`），S3 阶段"S5 再说"的正是它。
读应答前先轮询 fn1 `0x04` bit7 —— 这就是上次 `pst=03f20206` 那个坑的解药：**有消息才读**，不盲读。

`AicCrc8` 与厂商 `crc8_ponl_107` 在全部 65536 组三字节输入上逐位一致；一帧
`DBG_MEM_READ_REQ(0x40500000)` = `10 00 11 d5 | 00 00 00 00 | 00 04 01 00 64 00 04 00 | 00 00 50 40`，
总长 20、len 字段 16（= 总长-4），与 `rwnx_set_cmd_tx` 的算术完全对上。

命令：`wififw read`（只读盘）/ `wififw id`（只读芯片版本 0x40500000，顺带证明 fn2 通道）/
`wififw write`（写 adid + patch_table，每 1 KB 块读回首字校验）。`write` 前强制先过
`chip id == aic8800d80` 和 `id` 两道闸。

### 09:45 真机 `aic fifo write fail at 0000`：字节模式的门没开

现场反差：probe 里**块模式** 1536 B 写成功，`wififw id` 的**字节模式** 20 B 消息第 0 字节就失败。

原因是厂商下载前有一组固定动作，我们此前一条都没做（`aicwf_sdiov3_func_init`，
`aicsdio.c:1820-1877`）：

| 步 | 动作 | 出处 |
|---|---|---|
| 1 | `sdio_set_block_size(func, 512)` → fn1 FBR `0x110/0x111 = 0x0200` | `:1833-1840` |
| 2 | `sdio_enable_func(func)` → CCCR `0x02` 置 fn1 位 | `:1841-1846` |
| 3 | `sdio_f0_writeb(func, 0x7F, 0xF2)` | `:1848-1855` |
| 4 | `aicwf_sdio_writeb(bytemode_enable_reg, 0x01)` → **fn1 `0x07 = 1`** | `:1869-1874` |

第 4 步是关键：厂商把 `0x07` 写成 1（注释写"1: no byte mode"，但 Linux 的
`sdio_writesb` 对 <512 B 恰恰走**字节模式**，`sdio_io.c:352-360`，所以这个位实际是
"允许字节模式"）。位没置时字节模式 CMD53 直接失败，块模式不受影响 —— 与真机现象完全吻合。

同时纠正两处**协议理解错误**（都会让 `id` 永远超时）：

1. **"有消息"不是 fn1 `0x04` 的 bit7**。厂商 D80 分支（`aicsdio.c:1505-1545`）用的是
   `intmaskf2 = status | 0x08`：`>120` 才是 fn2（消息）事件，`==127` 是字节模式
   （长度 = fn1 `0x05` × 4），否则块模式（长度 = `(status & 0x07) × 512`）。
   `0x77` 就没有 bit7 —— 照 bit7 判会永远等不到。
2. **RX 帧带 4 B 帧头**：`hdr[2] & 0x7f == 0x11` 才是 CFG_CMD_RSP（`aicsdio_txrxif.c:211-215`），
   `ipc_e2a_msg` 在 **+4**（id@+4、dest@+6、src@+8、param_len@+10、参数@+12）。
   之前把 id 当在偏移 0，也是错的。

### 09:59 按评审修三处（验收语义 + 日志通道）

1. **L1**：`SdhciMarkHex` 不再走 `HalSerialBootMark`（SLOG_BOOT 通道，`SERIAL_BOOT=0` 时整段消失），
   改成和明文一样经 `SdhciMark`（有 sink 走 sink、没 sink 直写 UART）。以前"明文在、Hex 没了"，
   而 CIS/OCR/`INT_STATUS`/`PRESENT_STATE`/CCCR 全是 Hex —— 排障正好缺证据。
2. **A1/A2**：FIFO 回环从枚举路径里**拿掉**。理由两条：1536 是固件消息补齐后的线上长度
   （时序表 §2.6），读长度应由 fn1 `0x04` 的待收块数决定，图案回读对不上属于正常；
   而且协议没开始就往发送 FIFO 推 1536 B 垃圾会污染 S5 的第一条消息。
   现在默认只打 `WiFi: sdio enum ok (cis)` + `WiFi: sdio fifo self-test skipped`，
   要跑得 `wififw fifotest`，且**成败都不改** `Host->Ready` / `gWifiSdioState`。
   **硬门禁：S3 验收线是 `WiFi: CIS manfid=c8a1:0082`，不是 `FIFO rw ok`。**
3. **P2**：`AicEnsureByteMode()` 在任何字节模式 CMD53 之前确认 fn1 `0x07` 的允许位（不再只靠
   vendor init 那一次）；置不上且卡又报 `SMB=0` 就明确失败，不静默换模式。

### 10:21 `status=01` 被误判成"没消息"：pending 判据三分支补齐

真机 10:13：`tx fc=04`（发成功）之后 `rx poll timeout status=01 masked=0009`，然后
`mem read cfm timeout`。芯片其实**早回了**，是判据只认 `>120` 把它判掉了。

厂商 D80 分支（`aicsdio.c:1505-1545`）是**三分支**，不是两分支：

| `masked = status|0x08` | 通道 | 长度 |
|---|---|---|
| `> 120` | **fn2**（msg） | `==127` 字节模式 `blen*4`；否则 `(status & 0x07)*512` |
| 其余（`!=0`） | **fn1**（data） | `==0x78` 字节模式 `blen*4`；否则 `(status & 0x7F)*512` |
| `== 0` | 无 | 继续轮询 |

`status=01` → `masked=9` → **fn1 块模式 1 块 = 512 B**。旧实现只写 `if (Masked > 120)`
就 return -1，把这一整支漏了。

现在：三个分支都实现，打 `WiFi: rx masked=09 status=01 fn=01 len=0200 mode=block`；
按算出的 len 从对应 fn 的 `0x0F` 读回，解析 `hdr[2]&0x7f`（0x11/0x12）、`id@+4`、`plen@+10`、
参数@+12，并打**前 32 字节 hexdump**（`WiFi: rx dump …`）供与厂商 `0x400` 请求逐字节比对。
`len>512` 只读首块并明示。

### 10:12 发送前流控 + 消息补齐 512（"写得进但芯片不回"的根因）

三条都来自厂商 `aic8800_fdrv/aicwf_sdio.c`：

1. **流控 credit（:101-139 `aicwf_sdio_flow_ctrl_msg`）**：发之前读 V3 的
   `FLOW_CTRL_Q1` = fn1 **`0x03`**，拿到可用 buffer 数；为 0 就按厂商节奏重试
   （<30 轮 200 µs、<40 轮 2 ms、其后 10 ms，共 `FLOW_CTRL_RETRY_COUNT`=50 轮）。
   没有 credit 就发 = 芯片 FIFO 没准备好，写进去也白写。
2. **长度补齐（:1230-1237）**：先 4 B 对齐，再补 `TAIL_LEN`=4 个 0 并**向上取整到 512**；
   整块时不补。厂商门槛是 `len < fc * BUFFER_SIZE`（`BUFFER_SIZE`=1536）。
3. **块模式发送（:1240-1262 → `aicwf_sdio_send_pkt` → `sdio_writesb`）**：补齐后整块写。
   我们走显式块模式 CMD53（Blocks = 补齐长度/512），块写被拒时按同长度退回字节模式，
   两次参数都打出来。

接收侧：超时不再只打 timeout，要打 `status= masked= pend= ctrl= blen= ioex=`
（`status|0x08 > 120` 那套判据不变）。

判读：`tx fc=` 是**芯片给的发送 credit**。`fc=0`（且重试 50 轮仍 0）= 芯片 ROM 没在监听，
不是发送格式问题，回头查 `fn0 0xF2` 之后的时序（含那段 `#if 0` 的 pad 序列）；
`fc>=1` 才说明消息通道活着，再看 `aic rx` 有没有回帧。

补充：`sdio_writesb` 对**恰好 512 B** 走的是字节模式（`sdio_io.c:326` 要求 `size > 512`），
所以块模式被拒时按同长度退回字节模式是对齐厂商行为的，不是退让。

### 10:07 `fbr110 OUT_OF_RANGE`：FBR 在 fn0 地址空间，不在 fn1

真机：`fbr110 FAIL cmd=52 arg=10022000 ... r5=00001100 OUT_OF_RANGE`。
`arg` 拆出来是 **fn1 + addr 0x110** —— 错在这里。

SDIO 规范：**CCCR(0x00-0xFF)、FBR(0x100-0x1FF)、CIA(0x1000+) 全在 fn0 的地址空间**，
用 `function=0 + 绝对地址` 访问。`0x110` 是"fn0 空间里、function 1 的 FBR 块长寄存器"；
fn1 自己根本没有 0x110 这个地址，所以卡回 OUT_OF_RANGE。Linux 同样：
`sdio_set_block_size() → mmc_io_rw_direct(card, 1, 0, SDIO_FBR_BASE(num)+0x10, ...)`，
`SDIO_FBR_BASE(f)=f*0x100`。

改了三处：

1. FBR 读写一律 `Fn=0` + 绝对 `0x110/0x111`（`AicWriteReg`/`AicMarkIoState` 都改）；
2. IOEx 只 `|= 0x02`（只保 fn1 位），不再写 `0x03` 顺手把 fn2 也开；
   bit0 回读不变按"只读/保留位"处理，日志标注不可信；
3. FBR/IOEx 变成**幂等复核**：枚举阶段（`Sdio.c:978-990`）本来就写过 FBR 和 IOEx，
   vendor init 只确认目标值 —— 关键在 fn0，不是"先拧 IOEx"。

失败时自动补一组 CCCR 快照（`fbr write failed, state:`），用来查"是否有复位把枚举态清掉"。

### 09:59 vendor init 的顺序修正（真机 09:54 的直接原因）

真机初值 `ioex=02` —— IOEx 的 bit(n-1) 才管 fn n（`sdio_enable_func`），所以 **fn1 一直是关的**，
对 fn1 的 CMD52 一律被拒，`fn1 blocksize write` 在第一步就死。厂商那边 fn1 早被 probe 打开了，
我们得自己开，而且**必须开在写 FBR 之前**。新顺序：`ioex |= 0x03` → FBR `0x110/0x111=0x0200`
→ `fn0 0xF2=0x7F` → `fn1 0x07=0x01`，每一步都 `old=/wrote=/now=` 读回；
失败时打 `cmd=52 arg= r5=(拆位) pst= int= err=`。

另：`SdhciDriverProbe` 里 `if (Host0->Ready) return 0;` 提前返回把 SD1 探测整段跳过了 ——
这就是"开机日志一行 `WiFi:` 都没有、只有手敲 wifiprobe 才探"的根因，已改成照样探 SD1。

为此新增：`AicWifiVendorPrepare()`（上面 4 步 + 前后各打一组寄存器）、
`AicMarkIoState()`（`fn0.f2 / cccr / ioex / iorx` + `fn1.bme / blen / misc / fbrblk`）、
`AicMarkXferParams()`（`bytes/blocks/blksz/blkcnt/tmode/pst`）。字节模式一失败就自动改用
块模式（补 0 到 512）重试一次，两次参数都打出来 —— "门没开"（两种都失败）与"写法不对"
（字节失败、块成功）一眼可分。

桌面验证：`/tmp/sdtest/harness.c` 用假 SDHCI + 假 SDIO 卡把 `Sdio.c`/`Io.c` 编到 x86 跑，
17 条断言（命令顺序、CMD52/53 参数、BLOCK_COUNT、CIS、1536 B 回环）全过 —— 只覆盖协议逻辑，不含电气时序。

## DRAM / DTB

- 有 DTB：信任 `a1`，Startup `DtbMemoryRegion`
- 无 DTB：`BoardConfig` `TOY_BOARD_RAM_BASE/SIZE`
- Common **不**解析 DTB

## QEMU 能验什么 / 不能验什么

| 能 | 不能 |
|----|------|
| `BOARD=milk-v-duo-s` 交叉编译通过 | QEMU virt 上跑 Duo S UART/MMIO |
| `make boards` 列出本包 | 在 CI 里自动出 `Blocks ready`（无物理板） |
| 默认 `BOARD=virt` smoke 不回归 | 代替真机 U-Boot `bootelf` |

## 验收日志摘录（2026-10-26 真机）

```text
board: milk-v-duo-s
vmm: skip MMU (board)
…
Fs: Vol RES (Resfs) / default=RES
Blocks 就绪。…
blocks>
```

配方：`fatload mmc 0:1 0x81800000 Kernel.elf` → `bootelf 0x81800000`。详档见上方排障历程。
