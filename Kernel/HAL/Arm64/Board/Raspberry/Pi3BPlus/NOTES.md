# Pi3BPlus · 踩坑备忘

> **排障正本（试错编年 · 少换卡）**：[`Documents/过程/排障/Pi3BPlus真机启动排障历程.md`](../../../../../../Documents/过程/排障/Pi3BPlus真机启动排障历程.md)  
> 本文只留板级短备忘；新尝试先记历程，再改代码。

## 串口

- 当前手测配置：`BOARD_UART_MINI=1`，`BOARD_UART_BASE = 0x3F215000`（**AUX mini-UART**）。  
- 证据：U-Boot 2021.01 在 **无** `dtoverlay=disable-bt` 时串口有字；加上则整卡无提示 → GPIO14/15 为 mini-UART，不是 PL011。  
- 40-pin：GPIO14/15 TX/RX；USB-TTL **3.3V**，共地；`enable_uart=1` + `core_freq=250`。  
- **禁止**无证据改波特率；沿用固件/U-Boot 已配好的 115200。  
- 若改走 PL011：须 `disable-bt` 且 U-Boot 也能在 PL011 出字后再改 `BoardConfig`。

## 启动 / EL

- **`u-boot.bin` = stock 2021.01**（`distro_bootcmd` → 自动 `/boot.scr`）。勿用 `bootflow scan` 新包当主二进制（停提示符要手敲）。  
- U-Boot `go`/`bootelf` 常停在 **EL2**。降 EL1 的 eret 在本板仍会挂（只见 `*` 不见 `+`）→ **暂留 EL2**；真机 **skip MMU**。  
- 入口信标：`*` → BSS → `+` → `Boot:`；另见 `vmm: skip MMU (board)`。  
- 早期 putc：先 `AUX_ENABLES|=1`，有限自旋。救砖两行见排障历程 §2。

## 固件 / DTB / DRAM（rpi-4）

- 卡上 **`pi.dtb` 的 `memory@0.reg` 常为 `<0 0>`**（未 patch 模板）→ 不能当真机 DRAM。  
- `boot.scr`：优先 `${fdt_addr}` / `${fdtcontroladdr}`（固件已 patch）`fdt move` → `0x02400000`；`bootelf` **不**把 DTB 放进 x0。  
- 内核：`x0` → `BOARD_DTB_LOAD` → **mailbox `GET_ARM_MEMORY`**（与 U-Boot `dram_init` 同源）→ 最后才 `BOARD_RAM_*`。  
- 验收：HDMI 屏上滚（勿只信 CoolTerm）见 `Boot: DTB|mbox …` + `regions=`。

## FB / 屏上日志（rpi-5）

- mailbox 要 FB → 深蓝铺底盖住 U-Boot Starting；`HalSerialGop` **全量**（`Hal/HalSerial/HalSerialGop.c`，对齐 X64）。  
- 编：`build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0 SCREEN_LOG=1`（`SCREEN_LOG=0` 则横幅/上滚编译关掉）。  
- 有 FB → VirtDesktop 模块表（非 RunFull）。

## 复位 / 关机拔卡

- **要拔 SD 刷卡**：`blocks>` 敲 `halt` 或 `shutdown` → firmware halt（`PM_RSTS|=0x555`，U-Boot/Linux 同源）→ 机子停住不重启 → **拔电源** → 拔卡。  
- **只想再跑一遍、不拔卡**：`reboot` 或 U-Boot `reset`。  
- 卡在 Failed、无提示符：只能拔电源。

## USB（rpi-7）

> **排障正本（试错编年）**：[`Documents/过程/排障/Pi3BPlus-DWC2-HID排障历程.md`](../../../../../../Documents/过程/排障/Pi3BPlus-DWC2-HID排障历程.md)

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

## 公司 vs 回家

- 公司（先 `source ~/Blocks/env.sh`）：
  - `build arm64 BOARD=virt` → `run-virt-arm --headless` 测公共层  
  - `build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0` 只验能编  
- 回家：SD + 串口才算 rpi-3 / rpi-4。
