# OpenBox — Kernel 侧脚本与开箱配置

Blocks 单仓里，**日常 QEMU** 优先 `Runtime/run.sh` / `sync.sh` / `smoke-boot.sh`。  
本目录是 **真机同步、virt 多架构、冒烟/expect、U 盘工具** 的权威脚本树（实现在 `Scripts/lib/`）。

## 布局

```text
OpenBox/
  Config.txt          # DEFAULT_TARGET=blocks；本机覆盖见 Scripts/Config.local.txt
  README.md           # 本文件
  Scripts/            # 入口按功能分子目录（无旧路径兼容壳）
    env.sh            # source 后短名：build / sync / run / smoke …
    build.sh          # OpenBox 编 Kernel/Boot（少用；日常 Kernel/build.sh）
    lib/              # 真实现（扁平，被入口 exec）
    sync/ run/ smoke/ test/ usb/ measure/
  archive/            # 一次性迁移工具（勿当日常）
```

## 功能分类（入口）

| 类 | 目录 | 常用 | 与 Runtime 关系 |
| --- | --- | --- | --- |
| 环境 | `Scripts/env.sh` | `source …/env.sh` | — |
| 同步/摆盘 | `Scripts/sync/` | **sync-nuc**（本机 BLOCKS）、**sync-usb**（U 盘）、prepare-rootfs | Runtime/sync.sh 只管编产物→双盘布局 |
| 运行 | `Scripts/run/` | run-split、run-virt-arm/riscv | **X64 日常用 Runtime/run.sh** |
| 冒烟 | `Scripts/smoke/` | smoke-virt / msc / install | **X64 日常用 Runtime/smoke-boot.sh** |
| 测试 | `Scripts/test/` | test-enosys、test-mod-verify… | expect 自动化 |
| U 盘 | `Scripts/usb/` | make-usb-stick | 首次分区；之后 sync-usb |
| 测量 | `Scripts/measure/` | measure boot | 开机计时 |
| 其它 | `Scripts/` 根 | bootstrap、desktop、store、pack、check-smells | 专项 |

## 能否再合并？

| 想法 | 结论 |
| --- | --- |
| 与 `Runtime/*.sh` 合成一套 | **不合并**。Runtime = QEMU 双盘日常；OpenBox = 真机/virt/expect 工具箱。短名 `env.sh` 已分流。 |
| `Scripts/` 薄入口 + `lib/` | **保留**。入口按功能分夹；实现仍扁平，避免 expect/内部路径大爆炸。 |
| `migrate` / `trim-edk2` / `install-to-root` | **已进 archive/**。三仓→ToyOS、裁 EDK2 的历史刀，日常勿跑。 |
| `DEFAULT_TARGET=toyos` | **已改 `blocks`**（`toyos` 仍作别名）。 |

## 真机一口令

```bash
source Kernel/OpenBox/Scripts/env.sh
build                          # → Kernel/build.sh
sync nuc                       # → Scripts/sync/sync-nuc.sh
# 或：./Kernel/OpenBox/Scripts/sync/sync-nuc.sh
```
