# Runtime — QEMU 运行布局（X64）

**架构**：Blocks Boot + 现网能力核（`PR-MERGE-kernel`）。资源布局合并自原 ToyImage。

```text
Runtime/
  Esp/X64/EFI/BOOT/BOOTX64.EFI   # Blocks Boot（disk0）
  RootFs/X64/Kernel.elf          # 现网能力核（disk1）
  RootFs/X64/BLOCKS.ID           # Boot 认卷
  RootFs/X64/TOYOS.ID            # 核内卷名兼容
  Assets/ Store/ Fw/ …           # 原 ToyImage 资源
  sync.sh / run.sh / smoke-boot.sh
```

## 用法

```bash
cd ~/Blocks/Boot && ./build.sh
cd ~/Blocks/Kernel && ./build.sh          # → Build/CodeA-HAL/X64/Kernel.elf
cd ~/Blocks/Runtime
./run.sh                 # 同步产物后起 QEMU（双盘 AHCI：Esp + RootFs）
./smoke-boot.sh          # 断言 ToyOS 就绪 + Desktop Ready
```

串口在启动 QEMU 的终端（`-serial stdio`）。提示符现网为 `toyos>`。

字体：`RootFs/X64/Assets/Fonts/CJK.TTF`（缺则 `ttf miss`）。图标等同目录下 `Assets/Icons/`。
