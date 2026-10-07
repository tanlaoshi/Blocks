# Boot

引导与固件侧。按架构分子目录：

| 目录 | 内容 |
| ---- | ---- |
| `X64/` | 原 ToyBoot 源码 + **`EDK2/` 工具包**（平级，无独立 `.git`） |
| `Arm64/` | Arm64 引导（待迁） |
| `RiscV/` | RiscV 引导（待迁） |
| `Build/` | 本侧构建产物（`Build/X64/BOOTX64.EFI`） |

```bash
cd Boot/X64 && ./build.sh
```
