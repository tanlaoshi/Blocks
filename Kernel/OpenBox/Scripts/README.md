# Scripts — 入口地图

实现一律在 **`lib/`**。子目录只放薄 `exec` 入口（**无**根上旧路径兼容壳）。

| 子目录 | 入口示例 |
| --- | --- |
| `sync/` | `sync-nuc.sh` `sync-usb.sh` `prepare-rootfs.sh` |
| `run/` | `run-split.sh` `run-virt-arm.sh` |
| `smoke/` | `smoke-boot.sh` `smoke-virt.sh` |
| `test/` | `test-enosys.sh` `test-mod-verify.sh` |
| `usb/` | `make-usb-stick.sh` |
| `measure/` | `measure-boot.sh` |

短名环境在**仓库根** `Blocks/env.sh`（`source ~/Blocks/env.sh`）。本目录根上保留：`env.sh`（转调根）、`build.sh` `bootstrap.sh` `desktop.sh` `store.sh` `pack.sh` `prepare-fs.sh` `check-smells.sh`。