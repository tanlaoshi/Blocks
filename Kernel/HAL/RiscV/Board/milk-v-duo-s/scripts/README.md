# Duo S 启动脚本（duo-boot）

目标：少抢 `soph#` 的 1 秒、少手打 `fatload`/`bootelf`。**不改**厂商 U-Boot 源码。

## 生成

```bash
python3 mk-boot-scr.py          # → boot.scr
# 有 u-boot-tools 时等价：
# mkimage -C none -A riscv -T script -d boot.cmd boot.scr
```

拷到卡上 **与 `Kernel.elf` 同 FAT**（`mmc 0:1` / LABEL=boot）：

```bash
cp boot.scr Kernel.elf /path/to/boot/
sync
```

## 用法（按串口所见分支）

### A. 冷启动自动（优先试）

上电**不要按键**。若默认 `bootcmd` 会扫 `boot.scr` / distro，应直接进 Blocks。

仍进 Linux → 本镜像未扫脚本，改用 B/C。

### B. 抢进 `soph#` 后一行

```text
fatload mmc 0:1 0x81800000 boot.scr
source 0x81800000
```

（比每次手打 fatload+bootelf 短。）

### C. 环境能落盘时加长 delay + 改 bootcmd

若日志是 `Loading Environment from FAT... OK`（**不是** `nowhere`）：

```text
setenv bootdelay 5
setenv bootcmd 'fatload mmc 0:1 0x81800000 Kernel.elf; bootelf 0x81800000'
saveenv
boot
```

`Loading Environment from nowhere` → **`saveenv` 无效**（见排障 P3）；不要指望持久 `bootdelay`，用 A/B。

## 禁止

- 格式化掉 `fip.bin` / `boot.sd`  
- 对 Duo 跑 `ck-pi-sd`  
