# Raspberry Pi 3B+ — Blocks SD 摆盘说明（rpi-2）
#
# 本目录由 prepare-sd.sh 生成，可整份拷到 SD 第一分区（FAT）。
# 厂商/社区 U-Boot **不改源码**；变量以板上 printenv 为准。
#
# 生成时间: 2026-10-27T01:26:27+08:00
# Kernel.elf: /home/tank/Blocks/Kernel/Build/HAL/Arm64/Kernel.elf
# .toy_board: rpi-3b-plus
#
# !!! 脚本成功 ≠ 真机 ready（rpi-3 串口手测）!!!

## 分区（对齐 X64：ESP/bootfs + BLOCKS）

- **bootfs**（FAT32 type 0x0c）：仅固件 — u-boot.bin / boot.scr / pi.dtb / config.txt（**无** Kernel.elf）
- **BLOCKS**（FAT32 type 0x0c）：Kernel.elf + BLOCKS.ID + Assets/Fonts + 根文件
- 整盘重做：`ck-pi-sd --repart` 或 `repart-pi-sd /dev/sdX`
- `boot.scr`：`load mmc 0:2 … Kernel.elf`（第二分区）

## 本目录文件（prepare 产出；CK 分流拷贝）

| 文件 | 去向 |
| --- | --- |
| Kernel.elf | → LABEL=BLOCKS |
| u-boot.bin | → bootfs（stock 2021.01；自动 boot.scr） |
| pi.dtb / boot.scr / boot.cmd / config.txt | → bootfs |

## U-Boot（上电自动）

`config.txt`：`kernel=u-boot.bin`。prepare 已把默认知 `bootdelay=-1` 改成 `-2` → 上电自动 `distro_bootcmd` → `/boot.scr`。  
**一般不用敲命令。** 卡上勿留旧 `uboot.env`。救砖 / 自动失败时再用 HDMI 键盘：

load mmc 0:1 0x1000000 boot.scr
source 0x1000000

成功标志：Starting application at 0x00080000，串口 * → + → Boot: …

排障编年：Archive/过程/排障/Pi3BPlus真机启动排障历程.md

## 禁止

- 把带 \`bootflow scan\` 的新 U-Boot 盖成 \`u-boot.bin\`（常停提示符）
- 在丢字的串口上敲长命令（易变成 lamm/otelf 等残句）
- load Kernel.elf 到 0x80000 再 bootelf（Starting at 0x0）

## 关于 booti

booti 需要 Linux Image 头。本柱交付 ELF / .bin，**不**强制 Image 封装。

## 验收

- 公司：本脚本退出 0 且 Kernel.elf 在 OUT/ 即过（rpi-2）
- 回家：拷 SD → 上电串口（rpi-3）；DTB memory（rpi-4）
