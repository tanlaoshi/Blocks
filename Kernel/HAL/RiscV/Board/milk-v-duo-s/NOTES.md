# Milk-V Duo S 笔记（PR-B3）

不要把秘密或专有固件 blob 提交进库。

**真机排障编年**（镜像 / 串口 / bootelf / `epc=0`）：  
[`Documents/过程/排障/MilkV-DuoS真机启动排障历程.md`](../../../../Documents/过程/排障/MilkV-DuoS真机启动排障历程.md)

## 硬件

- SoC：SG2000（CV181x）；本包目标核：**RISC-V C906**
- DRAM：约 512MiB @ `0x80000000`（U-Boot 常报 ~510 MiB）
- UART：USB-UART ↔ 板载调试口（常见 Pin：GND=6、TX=8、RX=10，以丝印为准）
- 启动介质：microSD（厂商镜像）或 TFTP

## 厂商 U-Boot

- 预装即可；**不改**源码，只改环境变量 / 卡上脚本 / 手工 `load`
- 常见：`kernel_addr_r=0x80200000`，`fdt_addr_r=0x81200000`
- 加载优先：`bootelf -p`（ELF）或 `go`（`.bin`）；`booti` 需 Linux Image 头（本柱不做）

## 启动 / 刷卡（duo-boot / autoboot / blocks-elf）

- `bootdelay=1` + 部分镜像 `ENV nowhere` → **`saveenv` 常存不住**。  
- 真机：`bootcmd=run sdboot` → **`boot.sd`**（不扫 `boot.scr`）。  
- **duo-fatload**：FIT `type=script` 真机失败（bootm 仍要 RISC-V Kernel Image）；上电仍 **embed FIT**；抢壳 `boot.scr` 才是 fatload BLOCKS。  
- **应用卷**：`source env.sh && ck-duo-sd` → **`Kernel.elf`→LABEL=BLOCKS**。  
- **禁止** `ck-pi-sd`。详见 [`scripts/README.md`](scripts/README.md)。

## µSD（duo-block）

- 控制器：`sdhci0` / `sophgo,cv1800b-dwcmshc` @ **`0x04310000`**（上游 `cv18xx.dtsi`）。PIO 读 CMD17 / 写 CMD24。  
- 复位后厂商区：Linux `cv18xx_sdhci_reset`（latency 1T、PHY TX bypass、TX/RX 时钟反相）。SD 槽**不**置 `EMMC_FUNC_EN`。  
- 基钟不用 CAPABILITIES（该字段坏）：按 U-Boot `cv1800b_sdhci.c` 的 **375 MHz** 做分频。识别 400 kHz，传输 25 MHz，不进 1.8V / 不调相。  
- `rdtime` 按 `cv18xx.dtsi` **25 MHz**。  
- 不发擦除。FAT 有 `fip.bin`/`boot.sd` 时卷名常叫 ESP——**勿故意写坏 boot**；默认卷 BLOCKS。  
- `exec HELLO.ELF` / `Kernel.elf`：放 **LABEL=BLOCKS**（与 X64/Pi 同）。  
- 卡检测位为 0 则放弃（`Boot: SD no card`）。U-Boot 刚 `fatload` 过却报无卡，把 `pst=` 记下来再改。

## DRAM / DTB

- 有 DTB：信任 `a1`，Startup `DtbMemoryRegion`
- 无 DTB：`BoardConfig` `TOY_BOARD_RAM_BASE/SIZE`
- Common **不**解析 DTB

## QEMU 能验什么 / 不能验什么

| 能 | 不能 |
|----|------|
| `BOARD=milk-v-duo-s` 交叉编译通过 | QEMU virt 上跑 Duo S UART/MMIO |
| `make boards` 列出本包 | 在 CI 里自动出 `ToyOS ready`（无物理板） |
| 默认 `BOARD=virt` smoke 不回归 | 代替真机 U-Boot `bootelf` |

## 验收日志摘录（2026-10-26 真机）

```text
board: milk-v-duo-s
vmm: skip MMU (board)
…
Fs: Vol RES (Resfs) / default=RES
Blocks 就绪。…
blocks>
```

配方：`fatload mmc 0:1 0x81800000 Kernel.elf` → `bootelf 0x81800000`。详档见上方排障历程。
