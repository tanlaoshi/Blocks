# Scripts — Kernel 侧宿主机脚本与开箱配置

Blocks 单仓里，**日常 QEMU** 优先 `Runtime/run.sh` / `sync.sh` / `smoke-boot.sh`。  
本目录是 **真机同步、virt 多架构、冒烟/expect、U 盘工具** 的权威脚本树（实现在 `lib/`）。

> 原 `Kernel/OpenBox/` 已拍扁为本目录（2026-10-26）。

## 布局

```text
Blocks/env.sh         # 【仓库根】source 后短名：build / sync / run / smoke …
Kernel/Scripts/
  Config.txt          # DEFAULT_TARGET=blocks；本机覆盖见 Config.local.txt
  README.md           # 本文件
  lib/                # 真实现（扁平，被入口 exec）
  sync/ run/ smoke/ test/ usb/ measure/
  archive/            # 一次性迁移工具（勿当日常）
```

## 功能分类（入口）

| 类 | 目录 | 常用 | 与 Runtime 关系 |
| --- | --- | --- | --- |
| 环境 | 仓库根 `env.sh` | `source ~/Blocks/env.sh` | — |
| 同步/摆盘 | `sync/` | **sync-nuc**（本机 BLOCKS）、**sync-usb**（U 盘）、prepare-rootfs | Runtime/sync.sh 只管编产物→双盘布局 |
| 运行 | `run/` | run-split、run-virt-arm/riscv | **X64 日常用 Runtime/run.sh** |
| 冒烟 | `smoke/` | smoke-virt / msc / install | **X64 日常用 Runtime/smoke-boot.sh** |
| 测试 | `test/` | test-enosys、test-mod-verify… | expect 自动化 |
| U 盘 | `usb/` | make-usb-stick | 首次分区；之后 sync-usb |
| 测量 | `measure/` | measure boot | 开机计时 |
| 其它 | 本目录根 | bootstrap、desktop、store、pack、check-smells | 专项 |

## 能否再合并？

| 想法 | 结论 |
| --- | --- |
| 与 `Runtime/*.sh` 合成一套 | **不合并**。Runtime = QEMU 双盘日常；本目录 = 真机/virt/expect 工具箱。短名 `env.sh` 已分流。 |
| 薄入口 + `lib/` | **保留**。入口按功能分夹；实现仍扁平。 |
| `migrate` / `trim-edk2` / `install-to-root` | **已进 archive/**。日常勿跑。 |
