# Runtime — QEMU 运行布局（X64）

```text
Runtime/
  Esp/X64/EFI/BOOT/BOOTX64.EFI   # 启动盘（disk0）
  RootFs/X64/Kernel.elf          # 系统盘（disk1，含 TOYOS.ID）
  RootFs/X64/TOYOS.ID
  Fw/OVMF_VARS.fd.clean          # NVRAM 种子
  sync.sh                        # 从 Boot/Kernel Build 拷贝产物
  run.sh                         # 起 QEMU
```

## 用法

```bash
cd ~/Blocks/Boot && ./build.sh
cd ~/Blocks/Kernel && ./build.sh x64
cd ~/Blocks/Runtime
./run.sh                 # 开 GTK 窗口（看屏 + 终端仍有串口）
./run.sh --headless      # 无窗口，只看串口
./run.sh --clean-nvram   # 重置 OVMF 变量
./run.sh --kill          # 杀残留 QEMU
```

看 K1 右上角色块：用 **不要** `--headless` 的 `./run.sh`（本机有 `DISPLAY` 时会出窗）。

依赖：`qemu-system-x86_64`、`/usr/share/OVMF/OVMF_CODE_4M.fd`。

K1 预期串口类似：`handoff:` → `KernelMain: early ok` → `FB WxH` → `video self-test` → `[Mod] Serial` → `modules done; park`。
