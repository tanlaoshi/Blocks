# EDK2 裁剪说明（Boot 专用）

> **无 `.git`**。仅供编 `BOOTX64.EFI`（`BootPkg/Boot.dsc` → 只链 `MdePkg`）。

## 布局

```text
Boot/                 ← WORKSPACE
  BootPkg/            ← 真实包目录（DSC/INF/源码）
  EDK2/               ← 本裁剪树（本文件所在处）
```

`build.sh` 设置：

- `WORKSPACE=Boot/`
- `PACKAGES_PATH=Boot/:Boot/EDK2/`
- `CONF_PATH=Boot/EDK2/Conf/`（只用这一份，不在 `Boot/Conf` 另开）
- **不再**在 `EDK2/` 下软链 `ToyBoot` / `BootPkg`

## 保留

| 路径 | 用途 |
| ---- | ---- |
| `edksetup.sh` / `Conf/` / `BaseTools/` | 构建环境 |
| `MdePkg/Include` + `Boot.dsc` 用到的 `Library/*` | 唯一包依赖 |
| `License.txt` / `README-TRIM.md` | 许可与本说明 |

产物在 `Boot/Build/`（可删可再生），不写进本裁剪树。

## 已剔除（与编译无关）

上游文档/维护者列表、Windows `.bat`、`pip-requirements`、BaseTools 测试与手册、Brotli 多语言/测试数据、`MdePkg/Test`、未进 `Boot.dsc` 的 Library（含 MipiSysTLib）等。
