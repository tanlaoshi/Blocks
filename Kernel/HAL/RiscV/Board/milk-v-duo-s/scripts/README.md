# Duo S 启动脚本（duo-boot / duo-autoboot）

目标：少抢 / 不抢 `soph#`。**不改**厂商 U-Boot 源码；**不**赌 `saveenv`（常 ENV nowhere）。

## 真机默认链（已证）

```text
soph# printenv bootcmd
bootcmd=run sdboot
soph# printenv boot_targets
## Error: "boot_targets" not defined
```

`sdboot` = fatload `boot.sd` → `bootm ${uImage_addr}#config-<FDT_NO>`。  
**不会**扫 `boot.scr`。上电直进 = 换卡上的 `boot.sd`（厂商原件改名备份），不是照抄 Pi 的 distro 脚本路径。

## A. 上电直进（duo-autoboot · 推荐）

```bash
# 宿主（已 ./build.sh riscv BOARD=milk-v-duo-s）
python3 mk-boot-sd.py          # → boot.sd（FIT，内嵌 Kernel @ 0x80200000）
```

卡上 **LABEL=boot**（与 `fip.bin` 同 FAT）：

```bash
# 保留厂商 FIT，勿删 fip.bin
mv boot.sd boot.sd.vendor      # 仅首次；已有 .vendor 可跳过
cp /path/to/scripts/boot.sd .
# Kernel.elf / boot.scr 可仍放着（手测 / 回退）
sync
```

断电上电、**不按键** → 期望串口 `blocks>`。

若报 `Could not find configuration node`：在 `soph#` 执行 `printenv sdboot`，把 `#config-…` 后缀记下来（脚本已含常见 Duo S 名；缺则加进 `mk-boot-sd.py` 的 `CONFIG_NAMES` 再打一版）。

回退 Linux：`mv boot.sd.vendor boot.sd && sync`。

换内核后须**重跑** `mk-boot-sd.py` 再拷 `boot.sd`（FIT 内嵌镜像；只更 `Kernel.elf` 不会被 sdboot 加载）。

## B. 抢壳后一行（duo-boot · 已过）

```bash
python3 mk-boot-scr.py          # → boot.scr
cp boot.scr Kernel.elf /path/to/boot/
```

```text
fatload mmc 0:1 0x81800000 boot.scr
source 0x81800000
```

## C. 环境能落盘时（少见）

仅当日志是 `Loading Environment from FAT... OK`（**不是** `nowhere`）才可 `setenv`/`saveenv`。ENV nowhere → 用 A。

## 禁止

- 格式化掉 `fip.bin` / 清空 boot FAT  
- 删掉厂商 `boot.sd` 且不留 `.vendor` 又不成 FIT（变砖需重刷镜像）  
- 对 Duo 跑 `ck-pi-sd`  
