# Pi3BPlus · 踩坑备忘

## 串口

- 默认 `BOARD_UART_BASE = 0x3F201000`（BCM2837 **PL011 UART0**，ARM 物理地址）。  
- 树莓派 40-pin：GPIO14/15 常为 UART TX/RX；USB-TTL 须 **3.3V**，共地。  
- 部分镜像默认控制台在 **mini-UART**（AUX），不是 PL011：若上电无输出，先对 U-Boot/`config.txt`/`dtoverlay` 与 DTB `stdout-path`，再改基址或驱动——**禁止**无证据改波特率碰运气。

## 固件 / DTB

- DTB 名常见：`bcm2837-rpi-3-b-plus.dtb`（以你实际 SD 上文件为准）。  
- 加载地址以 U-Boot `printenv` 的 `kernel_addr_r` / `fdt_addr_r` 为准；`BoardConfig.h` 仅为文档与 fallback。

## 公司 vs 回家

- 公司（先 `source ~/Blocks/env.sh`）：
  - `build arm64 BOARD=virt` → `run-virt-arm --headless` 测公共层  
  - `build arm64 BOARD=Raspberry/Pi3BPlus LWIP=0` 只验能编  
- 回家：SD + 串口才算 rpi-3 / rpi-4。
