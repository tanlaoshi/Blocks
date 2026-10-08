# Kernel

自 `~/ToyOS/ToyKernel` **逐步迁入**。

## 与 Boot 的边界

- Boot 填好 `BOOT_INFO`，调用 **`KernelMain(const BOOT_INFO *Info)`**（声明在 `Boot/Include/BootInfo.h`）。
- Kernel 经 `Include/Core/BootInfo.h` 引用该 ABI，并提供 `BootInfoSet` / `BootInfoGet`。
- 勿再把「组 BootInfo」放回 Kernel 早期 C。

## 已有碎片

```text
Kernel/Include/{Abi,Core,Hal}/…
Kernel/Hal/{Arm64,RiscV,Virt}/…   # 板级头等（后续编 Kernel 用）
Kernel/Build/
```
