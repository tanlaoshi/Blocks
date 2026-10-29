# Runtime 快速开始

布局合并自原 ToyImage。权威说明见 [`README.md`](README.md)。

```bash
cd ~/Blocks/Boot && ./build.sh
cd ~/Blocks/Kernel && ./build.sh
cd ~/Blocks/Runtime
./run.sh                 # 默认同步后起 QEMU
./run.sh --headless      # 无窗口，串口在终端
./smoke-boot.sh          # 断言就绪
```

| 路径 | 角色 |
|------|------|
| `Esp/X64/` | disk0：ESP / `BOOTX64.EFI` |
| `RootFs/X64/` | disk1：`BLOCKS.ID` + `BLOCKS.ID` + `Kernel.elf` |
| `Assets/` `Store/` `Fw/` `Msc/` | 资源 / 商店 / OVMF / USB MSC 种子 |
