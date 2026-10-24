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

- U-Boot `go`/`bootelf` 常停在 **EL2**。降 EL1 的 eret 在本板仍会挂（只见 `*` 不见 `+`）→ **暂留 EL2**；真机 **skip MMU**。  
- 入口信标：`*` → BSS → `+` → `Boot:`；另见 `vmm: skip MMU (board)`。  
- 早期 putc：先 `AUX_ENABLES|=1`，有限自旋。手测命令见排障历程 §2。

## 固件 / DTB

- 卡上短名 **`pi.dtb`**（内容同 `bcm2710-rpi-3-b-plus.dtb`；优先官方 bcm2710，勿默认 bcm2837）。  
- 加载地址以板上 `printenv` 为准；`BoardConfig.h` 仅为文档与 fallback。

## 公司 vs 回家

- 公司（先 `source ~/Blocks/env.sh`）：
  - `build arm64 BOARD=virt` → `run-virt-arm --headless` 测公共层  
  - `build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0` 只验能编  
- 回家：SD + 串口才算 rpi-3 / rpi-4。
