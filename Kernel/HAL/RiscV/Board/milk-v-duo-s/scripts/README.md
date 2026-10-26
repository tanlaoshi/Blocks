# Duo S 启动 / 刷卡脚本

**Kernel.elf 只在 LABEL=BLOCKS**（与 X64 / Pi 对齐）。boot 分区**不**放 `Kernel.elf`。

## 分区

| LABEL | mmc | 内容 |
| --- | --- | --- |
| **boot** | `0:1` | `fip.bin`、`boot.sd`（FIT）、`duo.dtb`、`boot.scr`；**无 Kernel.elf** |
| **BLOCKS** | `0:4` | **`Kernel.elf`**（唯一）、`BLOCKS.ID`、`HELLO.ELF`… |

厂商 `bootcmd=run sdboot` → `bootm boot.sd#config-*`（不扫 `boot.scr`；ENV nowhere）。

## 上电路径（duo-fatload 结论）

| 路径 | 结果 |
| --- | --- |
| **默认 embed FIT** | ✅ 上电 `bootm` → 内嵌核（改核须重打 FIT） |
| FIT `type=script` | ❌ 真机：认出 Script 后仍要 RISC-V Kernel Image |
| 抢壳 `source boot.scr` | ✅ 真·`fatload mmc 0:4`（须打断 autoboot） |
| 改 bootcmd / fip | 刻意不做 |

`ck-duo-sd` 认 **boot+fip.bin 同盘的 BLOCKS**（避开本机 nvme `LABEL=BLOCKS`）。

## 一键

```bash
source ~/Blocks/env.sh
ck-duo-sd                 # BLOCKS/Kernel.elf + embed FIT / scr / dtb
ck-duo-sd --blocks-only   # 只更新 BLOCKS（上电仍是旧 embed 核）
ck-duo-sd --script-fit    # 勿用于上电；仅复现 script 失败
```

## 禁止

- 对 Duo 跑 `ck-pi-sd`  
- 格式化掉 `fip.bin` / 无 `.vendor` 删 `boot.sd`  
- 再往 boot 拷 `Kernel.elf`  
