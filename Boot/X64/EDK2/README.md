# EDK2（X64 工具包）

放裁剪后的 EDK2 树，与 `Boot/X64` 下原 ToyBoot 源码平级。

- 用途：编 `BOOTX64.EFI` 等  
- **不是** Runtime / Kernel 的依赖源码树  
- **纳入 OpenBox 单仓**：拷入时**去掉**树内 `.git` / submodule 元数据，不保留 EDK2 独立仓史  
- 迁入前另开清单确认（体积大，勿擅自整树拷）
