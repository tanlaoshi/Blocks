# Runtime

客人侧系统盘与运行脚本所在（RootFs、资源、商店包、QEMU / 刷盘入口等）。

- 接收 Boot / Kernel 构建产物，**不**在此树内编译内核  
- 需要临时打包时可建 `Staging/`
