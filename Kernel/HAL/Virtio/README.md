# HAL/Virtio — Arm64 / RiscV 共享 virt 设备（PR-R5）

QEMU `virt`：virtio-mmio / blk / net / input、ramfb、DTB。

- **HalVideo / 像素绘制**：[`HAL/Common/`](../Common/)（不在本目录）。  
- **MMIO 窗**：Arm64 用 `Virtio.c` 默认；RiscV 由 `-DVIRTIO_MMIO_*` 覆盖。

构建：`./build.sh arm64` / `./build.sh riscv`；验收 `./smoke-virt.sh`。
