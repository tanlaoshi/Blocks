# Duo S 启动 / 刷卡脚本

**Kernel.elf 只在 LABEL=BLOCKS**（与 X64 / Pi 对齐）。boot 分区**不**放 `Kernel.elf`。

## 分区

| LABEL | mmc | 内容 |
| --- | --- | --- |
| **boot** | `0:1` | `fip.bin`、`boot.sd`（FIT=**BootStub**）、`duo.dtb`、`boot.scr`；**无 Kernel.elf** |
| **BLOCKS** | `0:4` | **`Kernel.elf`**（唯一）、`BLOCKS.ID`、… |

厂商 `bootcmd=run sdboot` → `bootm boot.sd#config-*`（不扫 `boot.scr`；ENV nowhere）。

## 上电路径

| 路径 | 结果 |
| --- | --- |
| **默认 stub FIT** | ✅ `bootm` → BootStub@`0x83000000` → 读 `BLOCKS:Kernel.elf` → 跳 `0x80200000` |
| 整核 embed FIT | ✅ 旧路径（`--embed-kernel`）；改核须重打 FIT |
| FIT `type=script` | ❌ 真机失败 |
| 抢壳 `source boot.scr` | ✅ `fatload mmc 0:4`（须打断 autoboot） |

日常热更：`deploy-duo` → `ksend BLOCKS:Kernel.elf`（**不必**重刷 `boot.sd`）。  
stub 变更：`deploy-duo --refresh-stub` 或 `ck-duo-sd`。

## 一键

```bash
source ~/Blocks/env.sh
ck-duo-sd                 # BLOCKS/Kernel.elf + stub FIT / scr / dtb
ck-duo-sd --blocks-only   # 只更新 BLOCKS
ck-duo-sd --embed-kernel  # 旧整核 FIT
deploy-duo                # 热更 BLOCKS:Kernel.elf
deploy-duo --refresh-stub # 重打 stub → BOOT:boot.sd
```

## 禁止

- 对 Duo 跑 `ck-pi-sd`  
- 格式化掉 `fip.bin` / 无 `.vendor` 删 `boot.sd`  
- 再往 boot 拷 `Kernel.elf`  
