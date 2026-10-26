# Duo S 启动 / 刷卡脚本

**Kernel.elf 只在 LABEL=BLOCKS**（与 X64 / Pi 对齐）。boot 分区**不**放 `Kernel.elf`。

## 分区

| LABEL | mmc | 内容 |
| --- | --- | --- |
| **boot** | `0:1` | `fip.bin`、`boot.sd`（FIT）、`duo.dtb`、`boot.scr`；**无 Kernel.elf** |
| **BLOCKS** | `0:4` | **`Kernel.elf`**（唯一）、`BLOCKS.ID`、`HELLO.ELF`… |

厂商 `bootcmd=run sdboot` → `bootm boot.sd`（**不能**直接 fatload 另一分区的 ELF）。因此：

- **上电**：FIT 内嵌核，但由 **`BLOCKS/Kernel.elf` 打包**（`ck-duo-sd`）
- **抢壳 `source boot.scr`**：真·`fatload mmc 0:4 Kernel.elf` + `duo.dtb` + `bootelf -p`

卡上通常无散装厂商 `.dtb` → 用仓内 `duo.dtb`。

## 一键

```bash
source ~/Blocks/env.sh
# 已 build riscv BOARD=milk-v-duo-s
ck-duo-sd                 # BLOCKS/Kernel.elf + 刷 FIT/scr/dtb + 删 boot/Kernel.elf
ck-duo-sd --blocks-only   # 只更新 BLOCKS（上电 FIT 仍是旧核，慎用）
```

## 禁止

- 对 Duo 跑 `ck-pi-sd`  
- 格式化掉 `fip.bin` / 无 `.vendor` 删 `boot.sd`  
- 再往 boot 拷 `Kernel.elf`  
