# Pi3BPlus · 踩坑备忘

## 串口

- 当前手测配置：`BOARD_UART_MINI=1`，`BOARD_UART_BASE = 0x3F215000`（**AUX mini-UART**）。  
- 证据：U-Boot 2021.01 在 **无** `dtoverlay=disable-bt` 时串口有字；加上则整卡无提示 → GPIO14/15 为 mini-UART，不是 PL011。  
- 40-pin：GPIO14/15 TX/RX；USB-TTL **3.3V**，共地；`enable_uart=1` + `core_freq=250`。  
- **禁止**无证据改波特率；沿用固件/U-Boot 已配好的 115200。  
- 若改走 PL011：须 `disable-bt` 且 U-Boot 也能在 PL011 出字后再改 `BoardConfig`。

## 固件 / DTB

- DTB 名常见：`bcm2837-rpi-3-b-plus.dtb`（以你实际 SD 上文件为准）。  
- 加载地址以 U-Boot `printenv` 的 `kernel_addr_r` / `fdt_addr_r` 为准；`BoardConfig.h` 仅为文档与 fallback。

## 公司 vs 回家

- 公司（先 `source ~/Blocks/env.sh`）：
  - `build arm64 BOARD=virt` → `run-virt-arm --headless` 测公共层  
  - `build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0` 只验能编  
- 回家：SD + 串口才算 rpi-3 / rpi-4。
