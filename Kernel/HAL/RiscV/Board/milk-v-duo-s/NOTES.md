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
