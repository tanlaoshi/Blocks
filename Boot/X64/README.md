# Boot / X64

原 `ToyBoot` 源码 + 同级裁剪 **`EDK2/`** 工具包（无独立 `.git`）。

```text
X64/
  Boot*.c / Video / Boot.dsc / Boot.inf
  build.sh
  EDK2/          # BaseTools + MdePkg + …
    ToyBoot -> ..
```

## 编译

```bash
cd Boot/X64
./build.sh          # 默认无调试串口
./build.sh DEBUG=1
```

产物：`Boot/Build/X64/BOOTX64.EFI`（EDK 中间文件在 `EDK2/Build/`，勿提交）。
