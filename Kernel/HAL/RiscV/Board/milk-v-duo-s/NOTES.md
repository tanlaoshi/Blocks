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
- **net-5**：ARP 缓存 + ICMP echo（对齐 Virtio Arp/Protocol）；默认 IP `HAL_NET_IP_DEFAULT`（`10.0.2.15`）— 局域网请 `setip`；壳 `ping <ip>`。无 lwIP/DHCP（RiscV 无端口）。  
- 刷卡前 `.toy_board=milk-v-duo-s`。

## µSD（duo-block）

- 控制器：`sdhci0` / `sophgo,cv1800b-dwcmshc` @ **`0x04310000`**（上游 `cv18xx.dtsi`）。PIO 读 CMD17 / 写 CMD24。  
- 复位后厂商区：Linux `cv18xx_sdhci_reset`（latency 1T、PHY TX bypass、TX/RX 时钟反相）。SD 槽**不**置 `EMMC_FUNC_EN`。  
- 基钟不用 CAPABILITIES（该字段坏）：按 U-Boot `cv1800b_sdhci.c` 的 **375 MHz** 做分频。识别 400 kHz，传输 25 MHz，不进 1.8V / 不调相。  
- `rdtime` 按 `cv18xx.dtsi` **25 MHz**。  
- 不发擦除。FAT 有 `fip.bin`/`boot.sd` 时卷名常叫 ESP——**勿故意写坏 boot**；默认卷 BLOCKS。  
- `exec HELLO.ELF` / `Kernel.elf`：放 **LABEL=BLOCKS**（与 X64/Pi 同）。  
- 卡检测位为 0 则放弃（`Boot: SD no card`）。U-Boot 刚 `fatload` 过却报无卡，把 `pst=` 记下来再改。

## DRAM / DTB

- 有 DTB：信任 `a1`，Startup `DtbMemoryRegion`
- 无 DTB：`BoardConfig` `TOY_BOARD_RAM_BASE/SIZE`
- Common **不**解析 DTB

## QEMU 能验什么 / 不能验什么

| 能 | 不能 |
|----|------|
| `BOARD=milk-v-duo-s` 交叉编译通过 | QEMU virt 上跑 Duo S UART/MMIO |
| `make boards` 列出本包 | 在 CI 里自动出 `ToyOS ready`（无物理板） |
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
