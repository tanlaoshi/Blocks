# rpi-3b-plus · 踩坑备忘

> **排障正本（试错编年 · 少换卡）**：[`Archive/过程/排障/Pi3BPlus真机启动排障历程.md`](../../../../../Archive/过程/排障/Pi3BPlus真机启动排障历程.md)  
> 本文只留板级短备忘；新尝试先记历程，再改代码。

## 串口

- 当前手测配置：`BOARD_UART_MINI=1`，`BOARD_UART_BASE = 0x3F215000`（**AUX mini-UART**）。  
- 证据：U-Boot 2021.01 在 **无** `dtoverlay=disable-bt` 时串口有字；加上则整卡无提示 → GPIO14/15 为 mini-UART，不是 PL011。  
- 40-pin：GPIO14/15 TX/RX；USB-TTL **3.3V**，共地；`enable_uart=1` + `core_freq=250`。  
- **禁止**无证据改波特率；沿用固件/U-Boot 已配好的 115200。  
- 若改走 PL011：须 `disable-bt` 且 U-Boot 也能在 PL011 出字后再改 `BoardConfig`。

## 启动 / EL

- **`u-boot.bin`**：stock 2021.01 + prepare 把默认知 **`bootdelay=-1`→`-2`**（rpi-10；否则每次停提示符）。`distro_bootcmd` → `/boot.scr`。勿用 `bootflow scan` 新包；勿留旧 `uboot.env` 盖掉 -2。  
- U-Boot `go`/`bootelf` 常停在 **EL2**。**rpi-8**：`KernelEntry` 降 EL1（`HCR.RW` + `SCTLR_EL1` RES1 + **`SP_EL1`** + `eret`）；并 **开恒等 MMU**（外设窗 Device）——EL1+MMU off 会 alignment halt。  
- 入口信标：`*` →（降 EL）→ BSS → `+` → `Boot: EL=1` → `vmm: Arm64 MMU on`。  
- 早期 putc：先 `AUX_ENABLES|=1`，有限自旋。救砖两行见排障历程 §2。

## 固件 / DTB / DRAM（rpi-4）

- 卡上 **`pi.dtb` 的 `memory@0.reg` 常为 `<0 0>`**（未 patch 模板）→ 不能当真机 DRAM。  
- `boot.scr`：优先 `${fdt_addr}` / `${fdtcontroladdr}`（固件已 patch）`fdt move` → `0x02400000`；`bootelf` **不**把 DTB 放进 x0。  
- 内核：`x0` → `BOARD_DTB_LOAD` → **mailbox `GET_ARM_MEMORY`**（与 U-Boot `dram_init` 同源）→ 最后才 `BOARD_RAM_*`。  
- 验收：HDMI 屏上滚（勿只信 CoolTerm）见 `Boot: DTB|mbox …` + `regions=`。

## FB / 屏上日志（rpi-5 / rpi-9）

- mailbox 要 FB → 深蓝铺底盖住 U-Boot Starting；`HalSerialGop` **全量**（`Hal/HalSerial/HalSerialGop.c`，对齐 X64）。  
- **像素序 BGR（mailbox 0）**：对齐 `0x00RRGGBB`；勿设 RGB，否则设蓝见红。  
- **rpi-9**：`HalVideoEnableFbWc` 把 FB 页映 **MAIR Attr1 Device**（`HAL_PAGE_PWT`）；恒等表默认 Normal WB + 仅 dcache sync 易花屏。串口见 `Boot: FB-WC OK (MAIR Attr1 Device)`。  
- 编：`build arm64 BOARD=rpi-3b-plus LWIP=0 SCREEN_LOG=1`（`SCREEN_LOG=0` 则横幅/上滚编译关掉）。  
- 有 FB → VirtDesktop 模块表（非 RunFull）。

## 复位 / 关机拔卡

- **要拔 SD 刷卡**：`blocks>` 敲 `halt` 或 `shutdown` → firmware halt（`PM_RSTS|=0x555`，U-Boot/Linux 同源）→ 机子停住不重启 → **拔电源** → 拔卡。  
- **只想再跑一遍、不拔卡**：`reboot` 或 U-Boot `reset`。  
- 卡在 Failed、无提示符：只能拔电源。

## 墙钟（rpi-11）

- 无板载电池 RTC；`HalRtcGetTime` = **CNTV 软钟**（种子本地东八，任务栏 `HH:MM`）。  
- 勿把 mailbox `GET_CLOCK_RATE`（给 SDHCI 基频用）当墙钟。

## SD / 盘上 TTF（rpi-12）

- **µSD = SDHOST** @ **`0x3F202000`**（VC `sdhost@7e202000` / `sdhost_gpio48`）；对齐 U-Boot `bcm2835_sdhost`（CORE 钟分频 + PIO）。  
- **Arasan SDHCI** @ **`0x3F300000`**（`emmc_gpio34`）= **WiFi SDIO**，不是卡槽；打错则 CMD timeout → `default=RES` → `ttf miss`。  
- 本柱：`bcm-sdhost` PIO 只读 CMD17 → **FAT32 `BLOCKS`**（对齐 X64）。  
- 摆盘：`bootfs`=固件 only（u-boot/boot.scr/dtb）；**`Kernel.elf` + `BLOCKS.ID` + Assets（Fonts/Icons/Images）在 BLOCKS**；`boot.scr` `load mmc 0:2`。  
- 缺 `Assets/Icons` → 桌面纯色、无图标脸；`ck-pi-sd` 会拷 bmp48 + WALL.BMP。  
- 整盘重做：`ck-pi-sd --repart` / `repart-pi-sd`（两分区皆 FAT32 type 0x0c；勿留 0x83 空壳）。  
- Arm `HalFpu` 岛 + `TrueTypeRaster` aarch64；禁 RES/ELF 内嵌顶「盘上」。

## USB（rpi-7）

> **排障正本（试错编年）**：[`Archive/过程/排障/Pi3BPlus-DWC2-HID排障历程.md`](../../../../../Archive/过程/排障/Pi3BPlus-DWC2-HID排障历程.md)

- 控制器：DWC2 @ `0x3F980000`（BCM2835 §14；VC `0x7E980000`）。
- 3B+ 根口 **LAN9514**；外接键鼠在 hub FS 口 → **SPLIT**（已做）。
- **DMA**：`HCDMA` = `phys|0xC0000000`；否则 DESC 全 0 → SET_ADDR STALL。
- SPLIT：控制/批量 **`XACTPOS=ALL`**（MID→XACTERR `HCINT=0x82`）；FS ep0 **首包 MPS=8** 再 GET 18。
- 屏标：`HID vid=` → `mouse ep= ok` → `DriverInputAttach mouse`；跳过 hub 上 HS（网）。
- 光标：桌面后 Reveal；`PresentCursor` **必须** `FbDcacheSync`（VC≠CPU cache）；底边白块=HOLD 豆腐≠光标。
- 参考：规格 + U-Boot dwc2；**勿** EDK2 当主指南；未露光标勿狂泵 HID。
- 串口：`GPFSEL1`/`CNTL`/`LSR` 软侧 OK 而 CoolTerm 哑 → **GPIO14→TTL RX**；残字以 HDMI 录像为准。

## 串口（CoolTerm 无字时）

- mini-UART：GPIO14/15 ALT5；入口即开 CNTL TX/RX；`HalSerialInitialize` 再拉复用。
- CoolTerm：**115200 8N1**。线：`GPIO14 (TX) → USB-TTL RX`，`GPIO15 (RX) ← TTL TX`，共地，**3.3V**。
- 判读：上电 CoolTerm 应先有 **U-Boot**；再 `*` `+` `Blocks uart`；HOLD 时 `UART-HOLD`。
  - U-Boot 也没有 → 线/口/适配器（常见：只接了 RX、TX 空焊）。
  - 仅有 U-Boot、无 `*` → 报内核；勿先猜改波特（`core_freq=250` 已钉）。
- 桌面 `halt`：若敲的是 **HDMI 键盘** 进壳，**不能**证明串口 RX 通。
- `halt` 后唤醒：排针 **5（GPIO3）短接 9（GND）**。
- U-Boot `smsc95xx_eth address not set`：**可忽略**（扫 LAN9514）；要消：提示符 `setenv usbethaddr b8:27:eb:00:00:01` + `ethaddr` 同值 + `saveenv`（boot.cmd 里也有，仅覆盖 source 之后）。
- 对齐 X64：`blocks>`（ShellOwn）后 Log/BootMark **不再上串口**，进 `RUNTIME*.LOG`（`log` / `log save` 可看）。

## 公司 vs 回家

- 公司（先 `source ~/Blocks/env.sh`）：
  - `build arm64 BOARD=virt` → `run-virt-arm --headless` 测公共层  
  - `build arm64 BOARD=rpi-3b-plus LWIP=0` 只验能编  
- 回家：SD + 串口才算 rpi-3 / rpi-4。
