# EDK2 裁剪说明（ToyBoot 专用）

> **无 `.git`**。仅供编 `BOOTX64.EFI`（`ToyBoot/Boot.dsc` → 只链 `MdePkg`）。

## 保留

| 路径 | 用途 |
| ---- | ---- |
| `edksetup.sh` / `Conf/` / `BaseTools/` | 构建环境 |
| `MdePkg/Include` + `Boot.dsc` 用到的 `Library/*` | 唯一包依赖 |
| `ToyBoot/` | 符号链接 → `$TOYOS_ROOT/ToyBoot` |
| `Build/` | 产物（可删可再生） |
| `License.txt` / `README-TRIM.md` | 许可与本说明 |

## 已剔除（与编译无关）

上游文档/维护者列表、Windows `.bat`、`pip-requirements`、BaseTools 测试与手册、Brotli 多语言/测试数据、`MdePkg/Test`、未进 `Boot.dsc` 的 Library（含 MipiSysTLib）等。

## 再生成

```bash
bash "$TOYOS_ROOT/ToyKernel/OpenBox/trim-edk2.sh" "$EDK2_SRC" "$TOYOS_ROOT"
# 然后可再跑本目录的二次瘦身，或直接：
source "$TOYOS_ROOT/Scripts/env.sh" && build toyboot
```
