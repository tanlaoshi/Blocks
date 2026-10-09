# lwIP（Blocks 嵌入）

来自现网 `ToyKernel/ThirdParty/lwip` 的 **src/include + 最小 core 源**（K41）。

- 端口：`Kernel/Hal/X64/LwIp/`（`lwipopts.h` / `LwIpNetif` / `LwIpIcmp`）
- 门面：`Kernel/Core/Network/NetworkLwip.c`；Shell `lwip on|status`
- 构建：`./build.sh x64` 默认 `LWIP=1`；`LWIP=0` 可关
