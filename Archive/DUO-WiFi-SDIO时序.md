# DUO WiFi（AIC8800D80 / SDIO）移植前技术准备 · 时序表与实现计划

> **PR-29 产出**（路线图 1030「DUO 的 WiFi」第一步）。本轮**只读厂商代码、写文档，不写驱动**——板上还没法测。
> 目的：把「厂商驱动到底怎么跟这颗芯片说话」翻译成**可照抄的寄存器级时序**，让 PR-30 开工时不用再翻源码。
>
> 资料全部来自本机，**未联网**：
>
> | 用途 | 路径 |
> |---|---|
> | 厂商 SDIO/BSP 驱动 | `Source/duo-buildroot-sdk-v2-2.0.1/linux_5.10/drivers/net/wireless/aicsemi/aic8800/aic8800_bsp/` |
> | rwnx fullmac 协议 | 同树 `aic8800_fdrv/` |
> | 固件（新版，与驱动同源） | `Source/duo-buildroot-sdk-v2-2.0.1/device/generic/rootfs_overlay/duos/mnt/system/firmware/aic8800/` |
> | 设备树 | `Source/duo-buildroot-sdk-v2-2.0.1/build/boards/{default/dts/cv181x,cv181x/sg2000_milkv_duos_musl_riscv64_sd/dts_riscv}` |
> | 厂商 MMC 主机驱动 | `Source/duo-buildroot-sdk-v2-2.0.1/linux_5.10/drivers/mmc/host/cvitek/sdhci-cv181x.c` |
> | 手册 | `Source/SG2000_TRM_V1.0-alpha.pdf`（`pdftotext` → `/tmp/trm.txt`） |
> | 我们的板包 | `Kernel/HAL/RiscV/Board/milk-v-duo-s/` |
>
> 行号以本机 `Source/` 树为准（厂商代码只读，未改一行）。

---

## 0. 一页结论

1. **芯片是 fullmac 的 SDIO 从设备**，不是 USB、也不是 softmac。802.11 的 scan/assoc/加密/聚合全在固件里；
   主机只做三件事：**搬固件**、**收发命令/事件**、**搬数据帧**。
2. **只有 function 1 有流量**。function 2（`0x0182`）在 D80 上没有任何数据路径（推测厂商把 WLAN class 放在
   fn2 的 FBR 里，好让 `SDIO_DEVICE_CLASS(WLAN)` 匹配命中）；
   `aicsdio.c:290-294` 会先把 probe 到的 func2 **替换成 func1**，之后所有 CMD52/CMD53 都打 func1。
3. **固件下载走「DBG 任务」这条命令通道**，不是写固件文件到某个 flash 地址。芯片上电跑 bootrom，
   bootrom 在 func1 上实现 `DBG_MEM_READ/WRITE/BLOCK_WRITE/START_APP` 四条消息——这就是"下载协议"。
4. **正常工作模式只下载 4 个二进制**（adid / patch / patch_table 解析 / fmacfw）；`lmacfw_rf` 只在 RF 测试模式用；
   `aic_userconfig_*.txt` 不下给芯片，是**主机侧**解析成发射功率/晶振电容参数（`aicwf_compat_8800d80.c:38-61`）。
5. **1.8V 电压切换不需要**：厂商主机驱动给 wifi-sd 挂的是 `SDHCI_QUIRK2_NO_1_8_V`（`sdhci-cv181x.c:1222-1226`）。
   我们现有 `Sdhci/Card.c:33` 的「不进 1.8V」注释与厂商一致，不是缺口。
6. **SDIO 中断确实走 DAT1**（卡中断），不是一根独立 IRQ：卡拉 DAT1 → SDHCI 的 `INT_STATUS[8] Card Interrupt`
   → SDHCI 控制器中断线 → **PLIC 38**。两级都要通；只做卡侧或只做 PLIC 都不行。
   我们目前**一级都没有**（`Hal.c` 只开了 `STIE`，全仓无 PLIC 驱动）——详见 §5、§7。
7. **PR-30 可以完全不碰中断**：卡把状态放在 func1 `0x04`（misc int status）里，轮询 CMD52 就能收包
   （厂商 handler 本来就是读这个寄存器，`aicsdio.c:1506-1557`）。中断是吞吐优化，不是正确性前提。
8. **实际总线时钟是 50 MHz，不是 150 MHz**（这条结论推翻了看日志的第一印象）：
   - `FEATURE_SDIO_CLOCK 150 MHz`（`aic_bsp_driver.h:521`）只是 `aicbsp_get_feature()` 里的**一行打印**
     （`aic_bsp_driver.c:1952-1970`），真机 dmesg 那行就是它；
   - 真正去 `set_ios()` 应用这个值的两处代码（`aicsdio.c:1879-1887`、`aicwf_sdio.c:2377-2385`）
     **都嵌在外层 `#if 0` 里，从未编译**；
   - 于是时钟由 MMC 核决定：无 1.8V → UHS 分支不走（`sdio.c:848`）→ 走 HS 分支
     `mmc_set_clock(host, mmc_sdio_get_max_clock(card))`，而 HS 情况**硬编码 50 MHz**
     （`sdio.c:427-440`、`:865`）。
   - **结论：我们按 400 kHz 识别 → 50 MHz HS 传输即可，与厂商实测一致**。150 MHz 不存在，也不需要 tuning。

**先做、也最容易做错的一件事**（放最前面提醒）：wifi-sd 是 `non-removable`，没有卡检测脚。
我们现在的 `Sdhci/Card.c:41-46` 会读 `PRESENT_STATE.CARD_INSERTED`，读不到就直接返回失败。
必须先在 TOP 里把 SD1 的卡检测**覆盖成"有卡"**，否则后面全部白干（细节见 §2.1 第 3 步）。

---

## 1. 硬件事实表

### 1.1 控制器与中断

| 项 | 值 | 出处 |
|---|---|---|
| wifi-sd 控制器 | **SD1**，基址 `0x04320000`，窗口 64 KiB（`0x0432FFFF`） | TRM p.2364「0x04320000 / 0x0432FFFF / SD1 控制寄存器」；dts `reg = <0x0 0x4320000 0x0 0x1000>`（`cv181x_base.dtsi:535`） |
| compatible | `cvitek,cv181x-sdio` | `cv181x_base.dtsi:533`；真机 `/proc/device-tree/wifi*/compatible` 也是它 |
| 相邻控制器 | SD0 `0x04310000`（µSD，我们已在用）；DMA `0x04330000` | TRM p.2355-2370 |
| **中断号** | **PLIC 线 38**，`IRQ_TYPE_LEVEL_HIGH` | dts `cv181x_base_riscv.dtsi:341-343`（`interrupts = <38 ...>`）；**真机已确认**：`/proc/interrupts` → `25: 9328 T-Head PLIC 38 mmc2`（`Archive/路线图1030.md:200`） |
| PLIC 控制器 | `0x70000000`，`riscv,ndev = <101>`，`max-priority = <7>` | dts `cv181x_base_riscv.dtsi:69-79` |
| 控制器能力 quirk（厂商） | `SDHCI_QUIRK_BROKEN_ADMA`、`QUIRK2_NO_1_8_V`、`QUIRK2_BROKEN_64_BIT_DMA`、`QUIRK_CAP_CLOCK_BASE_BROKEN` | `sdhci-cv181x.c:1222-1226` |
| 基钟 | **375 MHz**（`src-frequency`），与 µSD 同 | dts `cv181x_base.dtsi:537`；我们已按 375 MHz 分频（`Sdhci/Private.h:18`） |
| 信号电压 | **仅 3.3 V**（`NO_1_8_V`）；dts 里的 `sd-uhs-*` 实际不会生效 | `sdhci-cv181x.c:1225`；dts `sg2000_..._sd.dts:74-82` |
| max-frequency | dts 声明 `187500000`（上界）；**实际时钟 50 MHz**（HS 分支硬编码） | dts:81；`sdio.c:427-440`、`:845-865`；150 MHz 是死代码（见 §0-8） |
| 总线宽度 | 4-bit（`bus-width = <4>`） | `cv181x_base.dtsi:534` |
| 卡检测 | `non-removable`（无 CD 脚） | `cv181x_base.dtsi:542` |
| pinmux | 由**厂商 U-Boot 无条件做**：`pinmux_config(PINMUX_SDIO1)`；`SD1_SDIO_PAD` 分支写 `TOP 0x03000294[10]=0` + `0x03001048/50/5C/54/60/4C/58` | u-boot `board/cvitek/cv181x/board.c:32`（`#define SD1_SDIO_PAD`）、`:89-95`、`:272` |

### 1.2 电源 / 复位 / 唤醒 GPIO

| 信号 | 值 | 出处 |
|---|---|---|
| **WiFi 电源使能** | **`GPIOA15`**（`&porta 15`，`GPIO_ACTIVE_HIGH`） | dts `sg2000_milkv_duos_sd.dts:84-88`（`&wifi_pin` 的 `poweron-gpio`） |
| 唤醒 | `GPIOE7`（`&porte 7`）——只有低功耗/`CONFIG_GPIO_WAKEUP` 才用；我们不做省电，**忽略** | 同上 `wakeup-gpio` |
| GPIO 控制器 | `gpio0 @ 0x03020000`（DW APB GPIO，bank A = `porta`，32 线） | `cv181x_base.dtsi:302-315` |
| 上电动作 | `gpio_direction_output(pin, 1)` + **`mdelay(50)`** | `aicsdio.c:516-521`（`CONFIG_PLATFORM_CVITEK` 段） |
| 掉电动作 | 输出 0 | `aicsdio.c:607-612` |
| 无独立 reset 脚 | 厂商只做电源循环；`reset_tx_rx_phy` 是控制器 PHY 复位，不是芯片复位 | dts:541；`aicsdio.c:579-615` |
| SD1 卡检测覆盖 | `TOP 0x03000294`：**bit8** `reg_sd1_carddet_ow`、**bit9** `reg_sd1_carddet_sw`；bit10 `reg_sd1_phy_sel`（0 = PWR_SD1 pad） | TRM p.14095-14115（`sd_ctrl_opt` @0x294） |

> **结论**：上电 = `GPIOA15` 拉高 + 等 50 ms + 覆盖卡检测。没有别的电源/时钟门控要开（SD1 时钟由
> 控制器 `CLOCK_CONTROL` 自己分频，不经过 `clkgen`，这点与以太网不同）。

### 1.3 SDIO 功能与身份

| 项 | 值 | 出处 |
|---|---|---|
| 厂商 ID | **`0xc8a1`**（func1/func2 相同） | `aicsdio.c:84-85`；真机 `/sys/bus/sdio/devices/mmc2:390b:{1,2}/vendor`（`路线图1030.md:257`） |
| 器件 ID | func1 = **`0x0082`**；func2 = `0x0182` | `aicsdio.c:89`、`:80`；真机 sysfs（`路线图1030.md:257-259`） |
| 引擎用哪个 function | **func1**（probe 在 func2 上触发后立刻换成 func1） | `aicsdio.c:290-294` |
| SDIO class 匹配 | `SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN)`（整类匹配，不看 ID） | `aicsdio.c:451-454` |
| 块大小 | **512** | `aicsdio.h:20` `SDIOWIFI_FUNC_BLOCKSIZE` |
| 芯片识别 | 读内存 `0x40500000`，`chip_rev = (memdata >> 16) & 0xFF` | `aic_bsp_driver.c:1925-1931` |
| 固件版本 | 读 `0x0012001C`（`RAM_FMAC_FW_ADDR + 0x01C`） | `aic8800d80_compat.c:154-161` |

### 1.4 固件文件（本机新版，与驱动同源）

目录：`Source/duo-buildroot-sdk-v2-2.0.1/device/generic/rootfs_overlay/duos/mnt/system/firmware/aic8800/`

| 文件 | 字节 | md5 | 用途 | 正常模式下载？ |
|---|---|---|---|---|
| `fmacfw_8800d80_u02.bin` | 331 348 | `13e6f0e58aae342d260d8672ab61c31f` | 主固件（fullmac 全部逻辑） | ✅ → `0x00120000` |
| `fw_adid_8800d80_u02.bin` | 1 708 | `f546881a81b960d89a672578eb45a809` | ADID 段 | ✅ → `0x00201940`（默认） |
| `fw_patch_8800d80_u02.bin` | 30 040 | `35d137b8a76daaeb4f5034df8e15bcde` | 补丁段 | ✅ → `0x0020b43c`（默认） |
| `fw_patch_table_8800d80_u02.bin` | 1 048 | `0c9bf9c9c10f7a90a22a4c35fa58c967` | 补丁表（**主机侧解析**，不整体下载） | ⚙️ 解析后按条目写 |
| `fmacfwbt_8800d80_u02.bin` | 333 388 | `a52b105588e7c4888ee64566d7fad866` | 带 BT 共存的主固件（`CONFIG_SDIO_BT` 时替代上一个） | ❌ 二选一 |
| `lmacfw_rf_8800d80_u02.bin` | 237 206 | `2fddfa919e6e16729d43316003391894` | RF 测试固件 | ❌ 仅 `AICBSP_CPMODE_TEST` |
| `aic_userconfig_8800d80.txt` | 2 683 | `35c8e99f3edd34d2a39bc9920e1da494` | 主机侧配置（功率表/晶振） | ❌ 不下给芯片 |

> 注意：真机 `/mnt/system/firmware/aic8800/` 里的文件大小（319 377 / 25 300 / 984 …，见 `路线图1030.md:713-720`）
> 与这里的**新版不同**。以本机树为准（与驱动同源），board 上的旧版留给 PR-33 对齐。
> 固件清单来源：`aic_bsp_main.c:143-163`（`fw_8800d80_u02[]` 四个字段：`bt_adid` / `bt_patch` / `bt_table` / `wl_fw`）。

---

## 2. SDIO 初始化时序

> 分四段：**0 上电/引脚 → 1 控制器 → 2 卡识别与 CCCR → 3 功能使能与 AIC 私有寄存器**。
> 每一段都标了厂商代码出处；「我们」列指出这一条是不是现有 `Sdhci/` 已经会的。

### 2.1 阶段 0：上电与引脚

| # | 动作 | 值 / 细节 | 出处 | 现有 |
|---|---|---|---|---|
| 0.1 | SD1 pinmux | 厂商 U-Boot **无条件**做（`PINMUX_SDIO1`，`SD1_SDIO_PAD`）。`TOP 0x03000294[10] = 0`；`pinmux 0x03001048/0x50/0x5C/0x54/0x60/0x4C/0x58 = SD1 功能` | u-boot `board.c:89-95`、`:272`；具体偏移由 U-Boot 的 `PINMUX_CONFIG` 按 `cv181x_pinlist_swconfig.h` 查表写，**我们不必重算** | 继承 U-Boot，**不用重做**；但要打一行 `TOP 0x294` 便于对照 |
| 0.2 | **WiFi 上电** | `GPIOA15`：`DDR(0x03020004) bit15 = 1`，`DR(0x03020000) bit15 = 1`，然后 **≥50 ms** | `aicsdio.c:516-521` | ❌ 没有 |
| 0.3 | **卡检测覆盖** | `TOP 0x03000294`：`bit8 = 1`（override）、`bit9 = 1`（software value = 有卡）。之后 `PRESENT_STATE[16] Card Inserted` 才会是 1 | TRM p.14095-14115；`non-removable` 见 dts:542 | ❌ **必须先做**，否则 `Card.c:41-46` 直接失败 |
| 0.4 | 等芯片就绪 | 厂商用 2000 ms 超时等 SDIO probe 信号量（`down_timeout(..., 2000)`） | `aicsdio.c:544` | 我们用 0.2 的 50 ms + 复位即可 |

> **为什么 0.3 必须做**：`Sdhci/Card.c:41-46` 的判据是 `CARD_STABLE && CARD_INSERTED`。
> wifi-sd 没有 CD 脚，SDHCI 的 present state 默认读 0 → 现在这份代码在 SD1 上 100% 失败。
> 两个选择：**(a)** 写 TOP 0x294 覆盖（推荐，物理上正确，`PRESENT_STATE` 也就对了）；
> **(b)** 给 SD1 实例加 `InstrumentNonRemovable` 旗标跳过这段。**(a) 更好**，因为后面 `CMD_INHIBIT` 等状态也更可信。

### 2.2 阶段 1：控制器初始化（基址换 `0x04320000`，其余照抄 µSD 的成功路径）

| # | 寄存器 | 值 | 出处 | 现有 |
|---|---|---|---|---|
| 1.1 | `SOFTWARE_RESET(0x2F)` | `0x01`（ALL），轮询到 0，超时 100 ms | `Command.c:6-13` | ✅ |
| 1.2 | 读 `P_VENDOR_AREA1(0xE8)` | 取 `& 0xFFF` 得 area（板上应打印出 `0x200`）；合法性检查 `>= 0x100` | `Command.c:16-26`（Linux 侧硬编码 `0x200`，`sdhci-cv181x.h:38`） | ✅（`Card.c:25` 会打印） |
| 1.3 | `area + 0x00` MSHC_CTRL | `\|= (1<<1)` **latency 1T** | `sdhci-cv181x.c:857-859` | ✅ `Command.c:27-28` |
| 1.4 | `area + 0x00` MSHC_CTRL | **`\|= (1<<16)`（"for sd1"）** | `sdhci-cv181x.c:860-863` | ❌ **缺这一位**（我们只对 SD0 验证过，SD0 不写它） |
| 1.5 | `area + 0x4C` PHY_CONFIG | `\|= (1<<0)` | `sdhci-cv181x.c:865-867` | ✅ |
| 1.6 | `area + 0x40` PHY_TX_RX_DLY | **`= 0x01000100`**（DS/HS 档；SDR104 档是 `BIT(8) \| tap<<16`） | `sdhci-cv181x.c:868-870` | ✅（`CV_PHY_DLY_BYPASS`） |
| 1.7 | `POWER_CONTROL(0x29)` | `0x0F`（bus power on + 3.3V） | `Card.c:27` | ✅ |
| 1.8 | `TIMEOUT_CONTROL(0x2E)` | `0x0E`（最大） | `Card.c:30` | ✅ |
| 1.9 | `INT_ENABLE(0x34)` / `SIGNAL_ENABLE(0x38)` | 先把两者都写 **0**，复位完成后再按需打开（厂商 `sdhci_cvi_reset_helper` 就是"先关中断→复位→恢复"） | `sdhci-cv181x.c:442-452` | ⚠️ `Card.c:31-32` 是 `INT_ENABLE=0xFFFFFFFF` + `SIGNAL_ENABLE=0`，能跑但不规范 |
| 1.10 | `HOST_CONTROL(0x28)` | `0`（1-bit 起步） | `Card.c:29` | ✅ |
| 1.11 | `CLOCK_CONTROL(0x2C)` | 375 MHz 基钟分频到 **400 kHz**，等 `INT_STABLE`，再置 `CARD_EN` | `Command.c:35-74` | ✅ |

### 2.3 阶段 2：SDIO 卡识别（CMD5 / CMD3 / CMD7 / CMD52）

这一段**厂商驱动里没有**——它由 Linux MMC 核在 `probe` 之前完成。我们必须自己实现。
下表是 Linux 核的实际动作顺序（`linux_5.10/drivers/mmc/core/sdio.c`、`sdio_ops.c`、`sdio_io.c`、`sdio_cis.c`）。

| # | 命令 | 参数 / 数据 | 说明 | 出处 |
|---|---|---|---|---|
| 2.1 | **CMD0** | 0 | 进 idle | `mmc_go_idle` |
| — | ~~CMD8 / ACMD41~~ | — | **不发**：`no-sd`/SDIO-only 路径没有 CMD8、没有 ACMD41。发了 CMD8 会超时，别当成错误（`sdio.c:730-800` 全流程里没有 `SD_SEND_IF_COND`） | `sdio.c:695-800` |
| 2.2 | **CMD5**（IO_SEND_OP_COND） | `arg = ocr`（主机可给的电压窗口，3.3 V 档取 `0x00300000`；先发一次 `0` 也能探活）轮询到 `R4[31] = 1` | R4：`[23:0]` OCR、`[24]` S18A(1.8V)、`[31]` ready | `sdio_ops.c: mmc_send_io_op_cond` |
| 2.3 | **CMD3** | 0 | 取 RCA（R6 `[31:16]`），之后所有命令带 RCA | `mmc_send_relative_addr`（`sdio.c:735-746`） |
| 2.4 | **CMD7** | RCA | 选中卡（`sdio.c:765`） | `mmc_select_card` |
| 2.5 | **CMD52 读 fn0 `0x00`** | — | CCCR/SDIO 版本（`[7:4]` CCCR rev，`[3:0]` SDIO rev）。**期望非 0** | `sdio_read_cccr`（`sdio.c:143`） |
| 2.6 | **CMD52 读 fn0 `0x08`** | — | Card Capability：`bit1 SMB`（支持 CMD53 多块）、`bit0 SDC`、`bit5 E4MI` | 同上 |
| 2.7 | **CMD52 读 fn0 `0x13`** | — | Speed：`bit0 SHS`（支持 High-Speed） | `sdio_enable_hs`（`sdio.c:412`） |
| 2.8 | **CMD52 写 fn0 `0x13`** | `\|= 0x02`（`SDIO_SPEED_EHS`） | 卡侧切 HS/SDR25（50 MHz）。**随后主机时钟提到 50 MHz**（`mmc_sdio_get_max_clock()` 在 HS 下硬编码 50000000，`sdio.c:427-440`）；SDHCI 的 `HOST_CONTROL` 不用改（默认就是 3.3V DS/HS 档） | `sdio.c:857-865`、`sdio.h:144` |
| 2.9 | **CMD52 读 fn0 `0x07`** → 写回 | `ctrl &= ~0x03; ctrl \|= 0x02`（4-bit） | 卡侧切 4-bit。**随后主机写 `HOST_CONTROL[1] = 1`**（顺序：先卡后主机） | `sdio.c:246-274`、`:355-371`、`:870-872` |
| 2.10 | **读 CIS**（见 §2.4） | — | 拿厂商/器件 ID、块大小 | `sdio_cis.c:246-350` |

> **关于 1.8V**：核里唯一的 UHS 入口是 `if (rocr & ocr & R4_18V_PRESENT)`（`sdio.c:714-731`）。
> 我们的 host 被 `NO_1_8_V` 摘掉了 UHS caps，`mmc_set_uhs_voltage()` 失败 → 核把
> `R4_18V_PRESENT` 从 `ocr` 里清掉，**继续以 3.3V 走 HS 分支**。这就是"dts 里写了 `sd-uhs-sdr104`
> 但板子照样跑"的原因——**不要照 dts 去实现 UHS**。

**CMD52 / CMD53 参数编码**（这两条命令我们一条都没有，PR-30 要新写）：

```
CMD52 (R5):
  bit31      R/W           1 = 写
  bit30:28   function      1..7（fn0 = CIA/CCCR/FBR）
  bit27      RAW           写后回读（一般 0）
  bit25:9    address       17 位寄存器地址
  bit7:0     data          读时为 0xFF 填充

CMD53 (R5):
  bit31      R/W
  bit30:28   function
  bit27      BLK_MODE      1 = 块模式（此时 bit8:0 是块数），0 = 字节模式（bit8:0 是字节数）
  bit26      OP_CODE       1 = 地址自增，0 = **固定地址（FIFO 必须用 0）**
  bit25:9    address
  bit8:0     count

R5 里的错误位（必须检查，否则会"看似成功其实没写进去"）：
  bit15 COM_CRC_ERROR / bit14 ILLEGAL_COMMAND / bit11 ERROR / bit9 FUNCTION_NUMBER / bit8 OUT_OF_RANGE
  （`include/linux/mmc/sdio.h:55-59`）
```

**SDHCI 侧要配的东西**：

| 项 | 值 |
|---|---|
| `COMMAND` 寄存器 flag | `RESP_SHORT(0x02) \| CRC(0x08) \| INDEX(0x10)`；CMD52/53 都要 CRC |
| CMD52 | `TRANSFER_MODE = 0`（无数据），等 `INT_RESPONSE` |
| CMD53 单块/多块 | `BLOCK_SIZE(0x04) = 512`；多块再写 `BLOCK_COUNT(0x06) = N`；`TRANSFER_MODE = BLK_CNT_EN(bit1) \| MSBS(bit5) \| (读? DTDS bit4)` |
| CMD53 块模式 | `COMMAND` flag 加 `DATA(0x20)`；等 `INT_DATA_END`，期间按 `INT_BUF_RD_READY/SPACE_AVAIL` 搬 PIO |
| 超时 | `TIMEOUT_CONTROL = 0x0E` 已是最长，够用（一次 3 块传输远小于 1 s） |

### 2.4 读 CIS 的流程（PR-30 的验收动作）

厂商驱动**不读 CIS**（Linux 核替它读了，`sdio_cis.c`）。我们得自己写，流程如下——**全部是 CMD52，不需要 CMD53**：

```
1) 取指针：CMD52 读 fn0 0x09 / 0x0A / 0x0B 三字节（LSB 先），拼成 17 位 ptr。
   公共 CIS 一般在 0x0000；function N 的 CIS 指针在 fn0 的 (N*0x100 + 0x09..0x0B)，
   即 fn1 → 读 0x109/0x10A/0x10B。

2) 遍历 tuple：
   ptr 处读 1 字节 = tpl_code
     0xFF → 结束
     0x00 → 空条目，ptr++ 继续
   读 ptr+1 = tpl_len
     0xFF → 结束
   连读 tpl_len 字节 = data[]
   ptr += 2 + tpl_len

3) 解析需要的 tuple：
   0x20 CISTPL_MANFID  → vendor = data[0] | data[1]<<8
                         device = data[2] | data[3]<<8      ← **验收就看这里**
   0x21 CISTPL_FUNCID  → data[0] = function class（6 = WLAN）
   0x22 CISTPL_FUNCE   → data[1] | data[2]<<8 = max block size
   0x15 CISTPL_VERS_1  → 版本字符串
   （`sdio_cis.c:233-240` 的 tuple 表；`cistpl_manfid` 在 `:78-100`）
```

**期望结果**（真机 sysfs 已验证，`路线图1030.md:257-259`）：

| CIS 来源 | vendor | device |
|---|---|---|
| 公共 CIS / fn1 CIS（`fn0 0x109`） | `0xc8a1` | **`0x0082`** |
| fn2 CIS（`fn0 0x209`） | `0xc8a1` | `0x0182` |

> 注意：`/sys/.../vendor` 的值来自 **CIS 的 MANFID tuple**（`sdio_cis.c:93`），
> 不是 FBR 里的 vendor 寄存器（FBR 只放 class，`sdio.c:72-95`）。所以"读 CIS 打印厂商 ID"这个判据是对的。

### 2.5 阶段 3：功能使能与 AIC 私有寄存器

厂商顺序（`aicwf_sdiov3_func_init`，**D80 专用路径**，`aicsdio.c:1819-1898`）：

| # | 动作 | 值 | 出处 | 备注 |
|---|---|---|---|---|
| 3.1 | 选 AIC 的寄存器映射表 | `aicwf_sdio_reg_init`：D80 用 **V3** 表 | `aicsdio.c:1710-1720` | 见下表 |
| 3.2 | 设 func1 块大小 | `sdio_set_block_size(fn1, 512)` → **CMD52 写 fn0 `0x110 = 0x00`、`0x111 = 0x02`** | `aicsdio.c:1835`；`sdio_io.c:159-183` | 注意是**写 fn0 的 FBR 区**，不是 fn1 |
| 3.3 | 使能 func1 | 读 fn0 `0x02`（IOEx）→ `\|= (1<<1)` → 写回；然后**轮询 fn0 `0x03`（IORx）bit1 = 1**，超时 `enable_timeout` | `aicsdio.c:1841`；`sdio_io.c:59-100` | 必须等 IOR，不能只写 IOEx |
| 3.4 | **写 fn0 `0xF2` = `0x7F`** | `sdio_f0_writeb(func, 0x7F, 0xF2)` | `aicsdio.c:1848-1853` | ⚠️ `0xF0-0xFF` 是 CCCR 的**厂商保留区**；Linux 为此设 `MMC_QUIRK_LENIENT_FN0`（`:1833`）。**值不明，照抄** |
| 3.5 | ~~设 150 MHz 时钟~~ | `feature.sdio_clock`（150 MHz）的 `set_ios` 应用段**在 `#if 0` 里**（`aicsdio.c:1859` 开 `#if 0`，`:1887-1890` 两层 `#endif` 收尾；fdrv 同款 `aicwf_sdio.c:2345/2389-2391`） | `aicsdio.c:1879-1890` | **D80 func init 阶段不设时钟**；实际 50 MHz 由 MMC 核在 HS 协商后设（§0-8）。我们照 50 MHz 做 |
| 3.6 | 关字节模式 | 写 **fn1 `0x07`（`SDIOWIFI_BYTEMODE_ENABLE_REG_V3`）= `0x01`**（1 = 关字节模式） | `aicsdio.c:1891-1895` | 即"只走块模式" |
| 3.7 | 小延时（可选） | D80 路径**没有**显式延时；非 D80 的 `aicwf_sdio_func_init` 在使能 func 后有 `udelay(100)` | `aicsdio.c:1759`（非 D80）、`:822`（流控里） | 我们照加无害；**但别把它当"等待芯片就绪"用**，芯片就绪的唯一证据是 CFM |

**AIC 私有寄存器映射（func1 地址空间，CMD52 访问）**：

| 用途 | D80（V3） | 老芯片（8801/DC/DW） | 说明 |
|---|---|---|---|
| 中断使能 | `0x00` | `0x04` | D80 写 `0x07` |
| 中断 pending | `0x01` | `0x05` | 读后清 `bit0`（soft irq） |
| 中断 to device | `0x02` | `0x09` | 主机→设备软中断 |
| 流控 Q1 | `0x03` | `0x0A` | **读它拿"还能收几帧"** |
| 杂项中断状态 | `0x04` | — | **D80 的收包入口** |
| 字节模式长度 | `0x05` / `0x06`(MSB) | `0x02` | `len = 值 × 4` |
| 字节模式开关 | `0x07` | `0x11` | 写 1 = 关 |
| 流控 Q2 | `0x09` | — | 未用 |
| **读 FIFO** | **`0x0F`** | `0x08` | CMD53 固定地址读 |
| **写 FIFO** | **`0x10`** | `0x07` | CMD53 固定地址写 |
| 寄存器块选择 | — | `0x0B` | **D80 不写这个**（`aicsdio.c:1789` 仅 DC/DW） |

出处：`aicsdio.h:24-48`（宏）、`aicsdio.c:1694-1721`（赋值）。

### 2.6 FIFO 收发（数据面基础）

| 方向 | 动作 | 细节 | 出处 |
|---|---|---|---|
| 写 | CMD53 fn1 → `0x10` | `sdio_writesb` → **`incr_addr = 0`（OP_CODE=0 固定地址）**；长度 1536 → 3 块 × 512（若卡 `SMB=1` 就一次多块，否则拆 3 次字节模式） | `aicsdio.c:742-751`；`sdio_io.c:519-525`；`sdio_io.c:326-360` |
| 写前 | 流控 | CMD52 读 fn1 `0x03`；`0` 就重试（50 次，200 µs → 1 ms → 10 ms 退避）；要求 `len <= 值 × 1536` | `aicsdio.c:693-729`、`:1014-1021` |
| 读 | CMD53 fn1 → `0x0F` | 长度 = `(intstatus & 0x7F) × 512`（块模式）或 `bytemode_len × 4`（`intstatus == 120`） | `aicsdio.c:1506-1549`、`:753-782` |
| 收包触发 | 读 fn1 `0x04` | `bit7 (0x80)` = "other interrupt"（先读 `0x01` 清、再写回 `& ~0x01`）；`[6:0]` = func1 待收块数，`120` = 字节模式；`>120` = func2（我们不用） | `aicsdio.c:1458`、`:1507-1549` |

**帧长对齐规则**（一个字节错就全崩，务必照抄）：

```
发送：payload_len = 8(头/哑字) + 8(lmac_msg 头) + param_len
      if (payload_len % 4)  补齐到 4 字节        (TX_ALIGNMENT = 4)
      if (payload_len % 512) 再补 4 字节(TAIL_LEN) 并向上取整到 512 的倍数
接上一条：所以 **1 KB 固件块的消息 = 1048 字节载荷 → 补到 1536 字节（3 块）**
          而 4 字节的 DBG_MEM_WRITE = 24 → 28 → **512 字节（1 块）**
```
出处：`aicsdio.c:987-1012`（`TAIL_LEN 4` 在 `aicsdio.h:55`，`TX_ALIGNMENT 4` 在 `aicsdio_txrxif.h:20`）。

### 2.7 中断（两级，缺一不可）

```
卡：事件到达 → 拉低 DAT1（SDIO 卡中断）
  ↓
SDHCI：INT_STATUS[8] = Card Interrupt（需 INT_ENABLE[8] + SIGNAL_ENABLE[8]）
  ↓
SoC：SD1 控制器中断线 → PLIC 38 → RISC-V S-mode 外部中断（sie.SEIE / scause=9）
  ↓
驱动：读 fn1 0x04 得到块数 → CMD53 读 FIFO → 解析 → 再读 0x04 直到 0
```

| # | 动作 | 值 | 出处 |
|---|---|---|---|
| 7.1 | 声明卡中断 | `sdio_claim_irq(fn1, handler)` → **CMD52 读 fn0 `0x04`（IENx），`\|= (1<<1) \| 1`（master）后写回** | `sdio_irq.c:299-333` |
| 7.2 | 再显式写一次 | `sdio_f0_writeb(fn1, 0x07, 0x04)` → fn0 `0x04 = 0x07`（master + fn1 + fn2） | `aicsdio.c:1367-1371` |
| 7.3 | 使能设备侧中断 | 写 fn1 `0x00 = 0x07` | `aicsdio.c:1379-1382` |
| 7.4 | 主机控制器 | `INT_ENABLE \|= (1<<8)`；`SIGNAL_ENABLE \|= (1<<8)`；必要时 `HOST_CONTROL[4]`（Interrupt At Block Gap）也置 1，否则多块传输期间卡中断会被吞 | SDHCI 规范；厂商靠 Linux 核默认行为 |
| 7.5 | 服务 | 读 fn1 `0x04` → 读 FIFO → 重复；`0x80` 位置位时先清 fn1 `0x01` 的 `bit0` 再写回 | `aicsdio.c:1514-1525` |

> **轮询替代方案（PR-30 用这个）**：不做 7.1-7.4，直接每 1 ms CMD52 读 fn1 `0x04`。
> 厂商 handler 本来就是"读 0x04 → 读 FIFO → 再读 0x04"，只是触发方式不同。
> 代价：~几十 µs 的 CMD52 开销 + 延迟，对 scan/assoc 完全够用。

---

## 3. 固件下载时序

### 3.1 总流程（D80 正常工作模式）

```
上电(0.2) → SDIO 枚举(§2) → func1 使能(§2.5)
   │
   ├─(1) DBG_MEM_READ 0x40500000  ──→ chip_rev = (data>>16)&0xFF
   │                                   U01→fw_8800d80_u01；U02/U03→fw_8800d80_u02
   ├─(2) system_config（D80 空表，什么都不写）
   │
   ├─(3) aicbt_init ── 蓝牙补丁（**WiFi 也要走**，因为同一个固件映像）
   │      ├ 3a 读 fw_patch_table_8800d80_u02.bin（1 KB，**只解析不下发**）
   │      ├ 3b 下发 fw_adid  → addr_adid（默认 0x00201940）
   │      ├ 3c 下发 fw_patch → addr_patch（默认 0x0020b43c）
   │      └ 3d 按表的每条 (addr,val) 用 DBG_MEM_WRITE 写下去（每条约 512 B 帧）
   │
   ├─(4) 下发 fmacfw_8800d80_u02.bin → 0x00120000
   ├─(5) patch_config：读固件里的三个指针，写"PTCH"补丁表
   ├─(6) sys_config（D80 空表）
   └─(7) DBG_START_APP(0x00120000, AUTO)  ──→ CFM.bootstatus = hwinfo
                                    ↓
                            固件开始跑 → rwnx 命令通道可用（§4）
```

出处：`aic_bsp_driver.c:1852-1950`（`aicbsp_driver_fw_init`，D80 分支 `:1925-1945`）、
`aic_bsp_driver.c:1726-1804`（`aicwifi_init`，D80 分支 `:1775-1795`）。

### 3.2 每一步的寄存器级细节

#### (1) 读芯片版本

| 项 | 值 |
|---|---|
| 消息 | `DBG_MEM_READ_REQ` = **1024**，param `{memaddr}` |
| 参数 | `memaddr = 0x40500000` |
| 回 | `DBG_MEM_READ_CFM` = **1025**，param `{memaddr, memdata}` |
| 解析 | `chip_rev = (memdata >> 16) & 0xFF`（D80 不做 `& 0x3F`） |
| 出处 | `aic_bsp_driver.c:1925-1937`、`:379-395` |

#### (2) 下片段

统一由 `rwnx_plat_bin_fw_upload_android(sdiodev, addr, filename)` 完成（`aic_bsp_driver.c:1233-1280`）：

| 项 | 值 |
|---|---|
| 消息 | `DBG_MEM_BLOCK_WRITE_REQ` = **1035**，param `{memaddr, memsize, memdata[1024]}` |
| **每次块大小** | **1024 字节**（`size > 1024` 时循环；末尾余数单独一次） |
| 循环写法 | `for (i = 0; i < size - 1024; i += 1024) write(addr+i, 1024, buf+i/4)`，然后 `write(addr+i, size-i, ...)` |
| 每次调用次数 | `⌈size / 1024⌉` |
| **线上帧长** | **恒为 1536 字节**（param 结构固定 1032 字节，末块也不缩——见 §2.6） |
| 段间延时 | **无**。每次都同步等 CFM（超时 3000 ms），CFM 到了就发下一块 |
| 出处 | `aic_bsp_driver.c:1255-1270`、`:359-377`、`:285-310`（`rwnx_msg_zalloc` 固定 `param_len`） |

实际每段的调用次数：

| 文件 | 字节 | 1024 B 次数 | 末次余数 | 总次数 |
|---|---|---|---|---|
| `fw_adid_8800d80_u02.bin` | 1 708 | 1 | 684 | **2** |
| `fw_patch_8800d80_u02.bin` | 30 040 | 29 | 344 | **30** |
| `fmacfw_8800d80_u02.bin` | 331 348 | 323 | 596 | **324** |
| `lmacfw_rf_8800d80_u02.bin`（仅 RF 测试） | 237 206 | 231 | 614 | **232** |

#### (3) 段地址（D80）

| 段 | 宏 | U01 | U02/U03（Duo S 应走这支） |
|---|---|---|---|
| ADID | `FW_RAM_ADID_BASE_ADDR_8800D80(_U02)` | `0x002017E0` | **`0x00201940`** |
| PATCH | `FW_RAM_PATCH_BASE_ADDR_8800D80(_U02)` | `0x0020B2B0` | **`0x0020b43c`** |
| 主固件 | `RAM_FMAC_FW_ADDR` | `0x00120000` | `0x00120000` |

出处：`aic_bsp_driver.h:345`、`:356-359`。

> ⚠️ **这两个地址可能被补丁表覆盖**：`aicbt_patch_info_unpack()` 会把 `fw_patch_table` 里
> `AICBT_PT_INF` 条目的数据（`adid_addrinf` / `patch_addrinf` / `reset_addr` / `adid_flag_addr` …）
> memcpy 进 `patch_info`，**覆盖**默认值（`aic_bsp_driver.c:1361-1370`、`:1418-1422`）。
> 实现时：**先按默认值写死能跑通，再把表解出来做交叉验证**；两者不一致时以表为准。

#### (3d) 补丁表下发

| 项 | 值 |
|---|---|
| 表文件格式 | `"AICBT_PT_TAG"` 16 字节 → 循环 `{name[16], type(u32), len(u32), data[len*8]}` |
| 特殊 type | `AICBT_PT_BTMODE`（先改 data 再下发）、`AICBT_PT_VER`（只打印版本）、`AICBT_PT_PWRON`（下发后 **`udelay(500)`**） |
| 下发方式 | 每条 `(addr, val)` 一个 `DBG_MEM_WRITE_REQ`（1026），**每次 512 字节帧** |
| 出处 | `aic_bsp_driver.c:1297-1360`（解析）、`:1469-1517`（下发，`udelay(500)` 在 `:1510-1511`） |

#### (4) 主固件

`DBG_MEM_BLOCK_WRITE` 逐 1 KB 写到 `0x00120000`，324 次（≈ 324 × 1536 B ≈ 486 KB 线上流量）。
按 50 MHz 4-bit（≈ 25 MB/s 理论，实测约 1/3）粗算 **0.05–0.1 s**；按 400 kHz 1-bit 则会到 **~12 s**——
所以 §2.3 的 4-bit + HS 切换是**功能问题，不只是性能问题**（超时是按命令 3 s 算的）。

#### (5) patch_config（`aic8800d80_compat.c:114-250`）——**顺序不能变**

```
前提：固件已下到 0x00120000（bootrom 还没跳过去执行）

5.1  DBG_MEM_READ 0x00120198  → config_base        （固件里"配置区"指针）
5.2  DBG_MEM_READ 0x001201A0  → patch_str_base     （"补丁字符串区"指针）
5.3  DBG_MEM_READ 0x0012001C  → fw_version_uint    （记下来，PR-31 验收用）
5.4  if (fw_version_uint > 0x06090100)             ← 新版固件走这支
        DBG_MEM_READ 0x001201A4 → patch_buff_base
        start_addr = patch_addr = patch_buff_base  （否则 start_addr = 0x0016F800）
5.5  DBG_MEM_WRITE patch_str_base + 0x00 = 0x48435450   "PTCH"
5.6  DBG_MEM_WRITE patch_str_base + 0x08 = 0x50544348   "HCTP"
5.7  DBG_MEM_WRITE patch_str_base + 0x04 = patch_addr
5.8  DBG_MEM_WRITE patch_str_base + 0x0C = patch_cnt
5.9  for i in 0..patch_cnt-1:                      ← patch_tbl_8800d80（`:58-81`，正常模式 1 条）
        DBG_MEM_WRITE patch_addr + 8i     = key + config_base
        DBG_MEM_WRITE patch_addr + 8i + 4 = value
     正常模式只有一条：key=0x00b4, val=0xf3010000（`USE_5G` 未定义时）
5.10 DBG_MEM_WRITE patch_str_base + 0x30 / 0x34 / 0x38 / 0x3C = 0   ← block_size[0..3] 清零
```

`patch_str_base` 的结构（`aic_patch_t`，`aic8800d80_compat.c:13-21`）：

| 偏移 | 字段 | 我们写的值 |
|---|---|---|
| `0x00` | `magic_num` | `0x48435450`（"PTCH"） |
| `0x04` | `pair_start` | `patch_addr` |
| `0x08` | `magic_num_2` | `0x50544348`（"HCTP"） |
| `0x0C` | `pair_count` | `patch_cnt` |
| `0x10..0x1C` | `block_dst[4]` | 不动 |
| `0x20..0x2C` | `block_src[4]` | 不动 |
| `0x30..0x3C` | `block_size[4]` | **全 0** |

> 这 4 次清零写在 `patch_str_base + 0x30`，而不是 `patch_addr`——因为 `AIC_PATCH_ADDR(mem)` =
> `aic_patch_str_base + offsetof(...)`（`aic8800d80_compat.c:24`）。别照着 `patch_addr` 算。

#### (6) sys_config

D80 的两张表都是**空的**（`aicbsp_syscfg_tbl_8800d80[]` = `{}`，`aic8800d80_compat.c:26-27`；
`gpio_cfg_tbl_8800d40d80` 只在 `CONFIG_OOB` 下编进来，`:83-93`）。→ **这一步实际什么都不写，可以留空函数。**

#### (7) 结束握手

| 项 | 值 |
|---|---|
| 消息 | `DBG_START_APP_REQ` = **1037**，param `{boot_addr, boot_type}`（8 字节） |
| 参数 | `boot_addr = 0x00120000`（`RAM_FMAC_FW_ADDR`），`boot_type = HOST_START_APP_AUTO = 1` |
| 回 | `DBG_START_APP_CFM` = **1038**，param `{bootstatus}` |
| 判据 | CFM 收到即成功；`bootstatus & 0xFF` 存为 `hwinfo_r` |
| 超时 | **3000 ms**（`RWNX_80211_CMD_TIMEOUT_MS`，`aic_bsp_driver.h:19`） |
| 出处 | `aic_bsp_driver.c:1546-1562`（`aicwifi_start_from_bootrom`）、`:437-...`（消息构造） |

> **等哪个寄存器/中断？** 没有寄存器可等——**等的是 CFM 消息**，从 SDIO 读 FIFO 收上来（§4.2）。
> 这是本移植最容易踩空的地方：固件"起来了"的唯一证据就是这一条 `DBG_START_APP_CFM`。

#### (8) 启动后的第一件事：wakeup 握手（fdrv 做的，不是 BSP）

固件跑起来以后，fdrv 的 `aicwf_sdiov3_func_init` 会再做一次**很轻的存活检查**（`aicwf_sdio.c:2394-2414`）：

| # | 动作 | 值 | 判据 |
|---|---|---|---|
| 8.1 | CMD52 写 fn1 `0x02`（`wakeup_reg`）= **`0x11`** | — | 写成功 |
| 8.2 | **`mdelay(5)`** | 5 ms | — |
| 8.3 | CMD52 读 fn1 `0x01`（`sleep_reg`） | — | **`val & 0x10` 非 0** → 打印 `sdio ready`；为 0 → 打印 `wakeup fail` |

> 这 3 步是**成本最低的"固件活着"探针**：bootrom 不会响应 `wakeup_reg`，
> 只有真正的固件才会把 `sleep_reg` 的 bit4 置起来。**建议把它当成 S4 的第一条验收**，
> 比发 `DBG_MEM_READ` 更早、更快、更明确。

### 3.3 时序/超时汇总

| 位置 | 值 | 出处 |
|---|---|---|
| 上电 → 可枚举 | ≥ 50 ms | `aicsdio.c:521` |
| 等 SDIO probe | 2000 ms 超时 | `aicsdio.c:544`、`:630` |
| func 使能后 | `udelay(100)` | `aicsdio.c:1759` |
| 每条命令等 CFM | **3000 ms**（队列里每多一条 ×N） | `aic_bsp_driver.c:127-129` |
| 命令队列上限 | 8 条 | `aic_bsp_driver.h:32` |
| CMD53 写完成等待 | 5000 ms（`CMD_TX_TIMEOUT`） | `aicsdio_txrxif.h:19` |
| 流控重试 | 50 次（200 µs/1 ms/10 ms 退避）× 10 轮 | `aicsdio.c:714-724`、`:1016-1020` |
| 补丁表 PWRON 延时 | `udelay(500)` | `aic_bsp_driver.c:1510-1511` |
| 固件版本分支阈值 | `> 0x06090100` 用新 patch buffer | `aic8800d80_compat.c:162` |

---

## 4. rwnx 命令通道

### 4.1 下行（主机 → 卡）帧格式

```
偏移  长度  内容
 0     2    hdr_len = (8 + param_len) + 4 = param_len + 12，取低 12 位
             （注意：不含这 4 字节头本身，也不含偏移 4 的哑字；最大 4095）
 2     1    0x11  （SDIO_TYPE_CFG_CMD_RSP；下行请求用它）
 3     1    D80: crc8_ponl_107(buf[0..2])，8801/DC/DW 填 0x00
 4     4    0x00000000（哑字，必须留）
 8     2    lmac_msg.id
10     2    lmac_msg.dest_id
12     2    lmac_msg.src_id
14     2    lmac_msg.param_len
16    ..    param[]
```
出处：`aic_bsp_driver.c:251-283`（`len` = `sizeof(lmac_msg) + param_len` = `param_len + 8`，
`aicwf_bus_txmsg(bus, buffer, len + 8)` → 送进 SDIO 的载荷 = `param_len + 16`）。
CRC8 多项式 `x^8+x^2+x+1`（`aicsdio.c:2004-2027`）。**D80 必须算 CRC，别的芯片填 0**（`:265-267`）——
漏了这条会被固件静默丢弃（没有 NAK、没有错误码）。

> 注：`buffer[0..1] = (len+4)`，其中 `len = sizeof(lmac_msg) + param_len = 8 + param_len`。
> 即头里的长度 = `param_len + 12`。`buffer[1]` 只取低 4 位（最大 4095）。

`lmac_msg` 本身 8 字节头：`id` / `dest_id` / `src_id` / `param_len`，之后紧跟 param（`aic_bsp_driver.h:76-82`）。

### 4.2 上行（卡 → 主机）帧格式

一次 CMD53 读回来的是一串**聚合帧**，循环解析（`aicwf_another_ptk`，`aicsdio_txrxif.c:133-148`）：

```
每帧：
 0    2   pkt_len（本帧后续字节数，不含这 2 字节）
 2    1   type：0x00 数据 / 0x10 CFG / 0x11 CFG_CMD_RSP / 0x12 CFG_DATA_CFM
           （代码里判类型时先 `& 0x7F` 掩掉 bit7，所以 0x91 也当 CFG_CMD_RSP）
 3    1   pad
 4    ..  载荷
            type = 0x11 → ipc_e2a_msg（= lmac_msg 结构），即命令/事件的 CFM
            type = 0x00 → 60 字节硬件头 + 802.11 帧
 对齐：非数据帧按 4 字节对齐；数据帧按 pkt_len + 60 再对齐到 4
```
出处：`aicsdio_txrxif.c:172-215`（BSP 侧）、`aicsdio_txrxif.h:22`（`RX_HWHRD_LEN 60`，注释 "58->60 word allined"）、
`aicsdio.h:61-66`（type 枚举）。

**收包循环**（D80，`aicsdio.c:1506-1558`）：

```
do { 读 fn1 0x04 → intstatus } while (读失败);          // 厂商写成了死循环，建议加 10 ms 超时
if (intstatus & 0x80) {                                // SDIO_OTHER_INTERRUPT：设备→主机软中断
    读 fn1 0x01 → v; 写 fn1 0x01 = v & ~0x01;
}
if (intstatus > 0) {
    块数 = intstatus & 0x7F;                           // bit7 是标志位，不是块数
    if (块数 == 0)         → 没有数据（只有软中断），不读 FIFO
    else if (intstatus == 120) → 字节模式：len = 字节模式长度寄存器(fn1 0x05,0x06) × 4
    else                  → 块模式：data_len = 块数 × 512，CMD53 读 fn1 0x0F
    解析聚合帧（§4.2 上半）；
}
```

> ⚠️ **厂商代码里有个坑，别照抄分支**：它算出 `intmaskf2 = intstatus | (1<<3)` 后用
> `if (intmaskf2 > 120) → 当 func2 报文` 来分流（`aicsdio.c:1530-1548`）。
> 于是"软中断 + 1 块数据"同时发生时（`intstatus = 0x81`），`intmaskf2 = 0x89 = 137 > 120`
> 会被误判成 func2，去读 fn2 的 FIFO。实际很少同时发生，所以厂商没暴露。
> **我们的做法**：先处理 bit7，然后**重新读一次 fn1 `0x04`** 拿干净的块数，
> 只走 func1 分支——语义等价且没有这个歧义。

### 4.3 命令号表（实测枚举值）

Linux 侧 `#define` 是 `LMAC_FIRST_MSG(task) = task << 10`，下面是**编译出来打印的真实数字**
（用本机 `lmac_msg.h` + `-DCONFIG_RWNX_FULLMAC` 跑出来的，不是手算）：

| Task | 值 | 说明 |
|---|---|---|
| `TASK_MM` | 0 | MAC 管理（复位/接口/信道） |
| `TASK_DBG` | 1 | 调试内存读写、启动固件 |
| `TASK_SCAN` | 2 | 扫描 |
| `TASK_ME` | 5 | MAC 引擎参数 |
| `TASK_SM` | 6 | 连接状态机 |
| `TASK_APM` | 7 | AP 模式 |
| `TASK_RXU` | 10 | 重排 |
| `TASK_API` | 13 | 厂商私有 |
| `DRV_TASK_ID` | 100 | 主机侧 src_id |

| 消息 | 值 | param 字节数 | 用途 |
|---|---|---|---|
| `MM_RESET_REQ` / `CFM` | **0 / 1** | 0 | 复位固件（boot 后第一条） |
| `MM_VERSION_REQ` / `CFM` | **4 / 5** | — | 取固件版本（**PR-31 验收首选**） |
| `MM_ADD_IF_REQ` / `CFM` | **6 / 7** | — | 加虚拟接口（STA） |
| `MM_START_REQ` / `CFM` | **2 / 3** | — | 启动 MAC |
| `MM_SET_MODE_REQ` | 28 | — | 模式 |
| `MM_SET_VIF_STATE_REQ` | 30 | — | 接口 up/down |
| `MM_SET_CHANNEL_REQ` | 16 | — | 信道 |
| `MM_SET_BSSID_REQ` | 24 | — | BSSID |
| `MM_SET_BASIC_RATES_REQ` | 22 | — | 基本速率集 |
| `MM_SET_EDCA_REQ` | 26 | — | EDCA |
| `MM_SET_SLOTTIME_REQ` | 32 | — | 时隙 |
| `MM_SET_IDLE_REQ` | 34 | — | idle |
| `MM_KEY_ADD_REQ` / `DEL` | 36 / 38 | — | 密钥（WPA2） |
| `MM_STA_ADD_REQ` / `DEL` | 10 / 12 | — | 对端 STA |
| `MM_SET_FILTER_REQ` | 14 | — | 收包过滤 |
| `MM_GET_MAC_ADDR_REQ` / `CFM` | **115 / 116** | CFM 6 | 取 MAC（**最小集合第一项**） |
| `MM_GET_FW_VERSION_REQ` / `CFM` | **128 / 129** | — | 固件版本 |
| `MM_GET_STA_INFO_REQ` | 117 | — | 链路质量 |
| `MM_SET_PS_MODE_REQ` | 49 | — | 省电（我们关） |
| `SCAN_START_REQ` / `CFM` / `SCAN_DONE_IND` | **2048 / 2049 / 2050** | req 376 | 扫描 |
| `SM_CONNECT_REQ` / `CFM` | **6144 / 6145** | req 320 | 关联 |
| `SM_DISCONNECT_REQ` / `CFM` | 6147 / 6148 | — | 断开 |
| `ME_CONFIG_REQ` | 5120 | — | 引擎参数（boot 后必发） |
| `ME_TX_CREDITS_UPDATE_IND` | 5131 | — | 发送信用 |
| `DBG_MEM_READ_REQ` / `CFM` | **1024 / 1025** | 4 / 8 | 读内存 |
| `DBG_MEM_WRITE_REQ` / `CFM` | **1026 / 1027** | 8 | 写内存 |
| `DBG_MEM_BLOCK_WRITE_REQ` / `CFM` | **1035 / 1036** | 1032 | 写块（固件下载） |
| `DBG_START_APP_REQ` / `CFM` | **1037 / 1038** | 8 / 4 | 启动固件 |

`HOST_START_APP_AUTO = 1`（普通启动），`HOST_START_APP_DUMMY = 5`。

### 4.4 最小可用集合

| 阶段 | 消息序列 | 判据 |
|---|---|---|
| **boot 后握手** | `MM_RESET_REQ(0)` → `MM_VERSION_REQ(4)` → `MM_GET_MAC_ADDR_REQ(115)` | 三条 CFM 都回，MAC 不是全 0/全 FF |
| **开接口** | `MM_ADD_IF_REQ(6)`（STA）→ `ME_CONFIG_REQ(5120)` → `MM_START_REQ(2)` | CFM 通过 |
| **扫描** | `MM_SET_CHANNEL_REQ(16)`（可选）→ `SCAN_START_REQ(2048)` → 等 `SCAN_DONE_IND(2050)` | `SCAN_DONE_IND` 里带 BSS 列表 |
| **关联** | `SM_CONNECT_REQ(6144)`（SSID/BSSID/信道/加密）→ 等 `SM_CONNECT_CFM(6145)` | CFM status = 0 |
| **收** | 数据帧 `type=0x00`，跳过 60 字节头 → 以太网帧 | 能 ping 通 |
| **发** | 数据帧 = 60 字节主机头（`struct txdesc_host`，`ipc_shared.h:197`）+ 802.11 帧，CMD53 写 fn1 `0x10` | 对端收到 |

发送前的**速率/功率/加密**都由固件管；我们要填的只有 60 字节头里的 `hostid`、`vif_idx`、`staid`、`flags`
（`ipc_shared.h:120-190` 的 `struct hostdesc`）。**PR-32 再细抠**，本轮只记录它的存在与大小。

### 4.5 CFM 匹配规则（必须照抄的语义）

| 规则 | 出处 |
|---|---|
| 每条命令按 `id` 找 `cmd->reqid` 相同的等待项；`id` 对上才算 CFM | `aic_bsp_driver.c:154-176` |
| 找不到匹配的 CFM = 异步**事件**（如 `SCAN_DONE_IND`），交给 event 回调，不能当错误 | `aic_bsp_driver.c:178-180` |
| 超时 = `3000 ms × 队列深度`；超时后整个 cmd_mgr 标记 CRASHED | `aic_bsp_driver.c:126-146` |
| CFM 的 param 拷进命令自带的 buffer，长度 `param_len` | `aic_bsp_driver.c:168-170` |
| 命令本身**无 ACK**（`WAIT_ACK` 被清掉），只有 CFM | `aic_bsp_driver.c:345-347` |

---

## 5. 与现有 `Sdhci/` 的差距

现状：`Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/`（6 个文件 1011 行），只做过 µSD（SD0）单卡 PIO。

| 能力 | 现状 | 证据 | 需要做什么 | PR |
|---|---|---|---|---|
| **多实例** | ❌ 单实例 | `Sdhci.h:14` 写死 `SDHCI_BASE 0x04310000`；`Sdhci/Sdhci.c:6-13` 全是全局变量；无 host 上下文 | 抽 `SDHCI_HOST`（Base/Area/Clock/Rca/BusWidth/CardType），SD0/SD1 各一份 | 30 |
| **SDIO 枚举** | ❌ 无 | `Card.c` 全是 SD 卡流程（CMD0/8/55/41/2/3/7/16） | 加 CMD5/CMD3/CMD7 + CCCR 读写路径 | 30 |
| **CMD52** | ❌ 无 | `Command.c:76` 只做通用发令，但没人发 52；`Transfer.c` 只有 17/24 | 新写 `SdhciIoRwDirect(fn, addr, write, data)` + R5 错误位检查 | 30 |
| **CMD53 单块** | ❌ 无 | 同上 | 新写 `SdhciIoRwExtended(fn, addr, incr, blk, cnt, buf)` | 30 |
| **CMD53 多块** | ❌ 无 | `Transfer.c:45` 只写 `BLOCK_SIZE`，`BLOCK_COUNT(0x06)` 定义了但**从未写**（`Sdhci.h:17`） | 写 `BLOCK_COUNT` + `TRANSFER_MODE` 的 `MSBS(bit5)` + 循环搬 PIO | 30 |
| **FIFO 固定地址** | ❌ 无 | — | `OP_CODE=0`（不自增）的长传输；一次 1536 字节要搬 384 个 32 位字 | 30 |
| **SDIO 卡中断** | ❌ 无 | `Card.c:32` 把 `SIGNAL_ENABLE` 写 0；`Card.c:31` 只清 `INT_STATUS` | `INT_ENABLE/SIGNAL_ENABLE` 的 bit8 + 收 DAT1；或先走轮询 | 30/31 |
| **PLIC / 外部中断** | ❌ **全仓没有** | `Kernel/HAL/RiscV/Hal/Hal.c:149,167` 只 `csrs sie, SIE_STIE`；`Trap.c:99-112` 只接 cause 5（S 定时器），其它中断只打印 `vmm: irq cause=` 就返回 → 电平中断会变成打印风暴 | 写 PLIC 驱动（`0x70000000`，enable/priority/claim/complete）+ `sie.SEIE` | 31 |
| **DMA** | ❌ 无（纯 PIO） | `Transfer.c:19` / `:67` 每次 128 个 32 位字（= 512 B/块） | 可后置；厂商 quirk 是 `BROKEN_ADMA`，只能用 **SDMA**（32 位地址、`BLOCK_SIZE` 的 bit16 边界） | 32 |
| **卡检测门禁** | ⚠️ 硬性要求插入 | `Card.c:41-46`（`CARD_STABLE` + `CARD_INSERTED`） | wifi-sd 无 CD → 写 `TOP 0x294` bit8/bit9 覆盖，或给实例加 non-removable 旗标 | 30 |
| **WiFi 电源 GPIO** | ❌ 无 | 板包里没有 GPIO 驱动调用 | `GPIOA15` DDR/DR + 50 ms | 30 |
| **4-bit 总线** | ⚠️ 只有 SD 卡路径 | `Card.c:111-115` 用 **ACMD6** 切宽度 | SDIO 要用 **CCCR 0x07** + `HOST_CONTROL[1]` | 30 |
| **厂商 MSHC 位** | ⚠️ 少一位 | `Command.c:27-28` 只置 `LATENCY_1T` | 补 `area+0x00` 的 **`bit16`（for sd1）** | 30 |
| **1.8V 切换** | ✅ 不需要 | `Card.c:33` 注释；厂商 `QUIRK2_NO_1_8_V`（`sdhci-cv181x.c:1225`） | 无 | — |
| **tuning** | ❌ 无 | `Command.c` 无 execute_tuning | 50 MHz 不需要；150 MHz 厂商自己都没启用 → **不做** | — |
| **超时** | ✅ 够 | `Card.c:30` `TIMEOUT_CONTROL=0x0E`（最大） | 无 | — |
| **时钟** | ✅ 够用 | `Command.c:44-53` 375 MHz 基钟分频；`TRANSFER_HERTZ=25 MHz` 要改 50 MHz（`Sdhci/Private.h:16`） | 换成 50 MHz 即可（实测工作点就是 50 MHz）；150 MHz 厂商未启用，不做 | — |
| **复位顺序** | ⚠️ 可改进 | `Command.c:6-13` 未先关中断 | 照抄 `sdhci-cv181x.c:442-452`（关 INT→复位→恢复） | 30 |

**一句话**：现有 `Sdhci` 是"SD 卡单实例 PIO 读块"的完整实现，**SDIO 需要的四件事（多实例、CMD52/53、FIFO 多块、卡中断）一件都没有**；
好消息是底层的寄存器访问、时钟分频、PIO 搬运、复位/PHY 配置都能直接复用。

---

## 6. PR-30 / 31 / 32 落地顺序与验收判据

> 路线图把"固件下载"放在 PR-30（总线层）里；下面按**可独立验收**切成 S1-S6 六段，
> 并标出对应 roadmap PR。每段都能单独上板、单独回退。

| 段 | 内容 | PR | **验收判据（串口必须打出这些字）** | 预估 |
|---|---|---|---|---|
| **S1** | 多实例重构 + SD1 基址/时钟/PHY + WiFi 上电 + 卡检测覆盖 | 30 | `Duo SDHCI1 ver=.. cap=.. area=200`、`SD1: present=..(CARD_INSERTED=1)`，`GPIOA15=1` | 0.5 d |
| **S2** | CMD52 / CMD53（单块 + 多块 + 固定地址 FIFO 语义） | 30 | 读 `fn0 0x00`（CCCR 版本）非 0；写 `fn0 0xF2=0x7F` 再读回 `0x7F`；写 `fn0 0x110/0x111` 读回 `00/02` | 1 d |
| **S3** | SDIO 枚举（CMD5/3/7）+ CCCR + 4-bit + HS + **读 CIS** | 30 ✅ | **`WiFi: CIS manfid=c8a1:0082`**（fn1）、`fn2=c8a1:0182`；`CCCR rev=.. caps=..(SMB=1)`；`HS=1 4bit=1` | 0.5 d |
| **S4** | AIC 私有初始化（fn0 0xF2 / fn1 0x07 / 块大小）+ **固件下载** + start_app + wakeup 握手 | 30 尾 / 31 头 | **`sdio ready`**（写 fn1 `0x02=0x11`，5 ms 后读 fn1 `0x01` 的 bit4 = 1）、**`WiFi: fwver=0609xxxx`**（读 `0x0012001C`）、`start ok bootstatus=..`；再发 `DBG_MEM_READ 0x40500000` 仍能回 | 1.5 d |
| **S5** | rwnx 命令通道 + `MM_RESET` → `MM_VERSION` → `GET_MAC` → `SCAN` → `CONNECT` | 31 | `WiFi: mac=..:..:..`、`scan: N bss`（至少一条带 SSID/信道/RSSI）、`assoc: status=0` | 2 d |
| **S6** | 数据路径（60 字节头 + 收发）+ `DriverNetAttach` + lwIP + DHCP | 32 | `wifi0: link up`、`dhcp: 192.168.x.y`、`ping 网关 ok` | 2 d |

**推荐顺序内的强制检查点**：

- **S3 不通过，绝对不要进 S4**。CIS 读不出来说明 CMD52 的地址/功能号/R5 解析有一处错，
  带着这个错去下载固件只会得到"每块都超时"，没有任何诊断信息。
- **S4 的第一次尝试建议只读不写**：先 `DBG_MEM_READ 0x40500000` 确认能通，
  再 `DBG_MEM_READ 0x0012001C`（此时是 bootrom 的垃圾值），**再**开始写 adid。
  一旦 `DBG_MEM_BLOCK_WRITE` 中途失败，芯片必须**重新上电**才能恢复（没有软复位路径）。
- **S5 的 `SCAN_DONE_IND` 可能比 `SCAN_START_CFM` 先到**：CFM 与事件共用一条队列，
  `id` 对不上就按事件处理（§4.5）。别把 `2050` 当错误。

**不在本轮 / 不在 PR-30 做的事**：
- **不做** 150 MHz（厂商自己都没启用，`set_ios` 段是 `#if 0`）与 tuning（50 MHz 不需要）
- DMA / SDMA（PIO 够跑 scan）
- 省电（`CONFIG_SDIO_PWRCTRL` / `MM_SET_PS_MODE`）——**明确关掉**
- 蓝牙（`aic8800_btlpm`）、`CONFIG_OOB`（DAT1 之外再用一根 GPIO 中断）
- 5 GHz（`patch_tbl_8800d80` 的 `0x00b4` 值区分 `USE_5G`；先 2.4 GHz）

---

## 7. 三个最可能卡住我们的技术点

### ① 没有可用的中断通路（两级都缺，且"卡中断走 DAT1"这件事本身反直觉）

- **卡侧**：SDIO 中断不是一根独立引脚，是**卡把 DAT1 拉低**表示"我有事"。
  主机必须在 SDHCI 里把 `INT_STATUS[8]`（Card Interrupt）打开（`INT_ENABLE` + `SIGNAL_ENABLE` 都要），
  然后去读卡的状态寄存器才知道是什么事。
  厂商：`sdio_claim_irq` → `sdio_irq.c:299-333`；`aicsdio.c:1367-1382`（D80 写 fn0 `0x04=0x07` 和 fn1 `0x00=0x07`）。
- **SoC 侧**：SDHCI 控制器中断线接 **PLIC 38**（dts `cv181x_base_riscv.dtsi:341-343`，真机 `路线图1030.md:200`）。
  我们**完全没有 PLIC 驱动**：`Kernel/HAL/RiscV/Hal/Hal.c:149,167` 只开了 `sie.STIE`（定时器），
  `Kernel/HAL/RiscV/Trap.c:99-112` 只接 cause 5（定时器），其它中断打印一行 `vmm: irq cause=` 就返回；
  电平中断没被清 → 会变成刷屏风暴并饿死调度。
- **为什么卡人**：SDIO 的 DAT1 是**电平**语义，卡会一直拉着直到主机读走数据。如果主机忘了清
  （读 fn1 `0x04` 直到 0），会立刻中断风暴；如果只清了 SDHCI 的 `INT_STATUS[8]` 而没读卡，风暴照旧。
- **绕法（强烈建议 PR-30 就这么做）**：不接中断，1 ms 轮询 fn1 `0x04`。厂商 handler 的核心就是
  "读 0x04 → 读 FIFO → 再读 0x04"（`aicsdio.c:1506-1557`），换成轮询**不需要改任何协议**。
  PR-30/31 全程轮询即可；中断留给 PR-32 做吞吐。

### ② CMD53 多块 + FIFO 固定地址 + 1536 字节帧对齐，三者必须同时对

- `sdio_writesb/readsb` 的 `incr_addr = 0`（`sdio_io.c:519-525`、`:502-507`）→ CMD53 **OP_CODE=0（固定地址）**。
  这个位如果置错（自增），第一次 512 字节之后的读会全部读到**递增的内存**而不是 FIFO，表现为
  "第一块对、后面全是垃圾"——极难从日志看出来。
- **每次传输不是 512 而是 1536**：固件块消息 `param_len = 1032`，加 8 字节头 = 1040，
  再按 `TAIL_LEN=4` 补到 512 的倍数 = 1536（`aicsdio.c:1007-1012`）。
  我们现在的 `Transfer.c` 只会搬 512 字节一次（`for (i=0;i<128;i++)`），必须改成"按块数循环"。
- `TRANSFER_MODE` 的 `MSBS(bit5)` + `BLK_CNT_EN(bit1)` + `BLOCK_COUNT(0x06)` 三个都得设，
  且**卡侧要 `SMB=1`**（CCCR `0x08` bit1，`sdio_io.c:326`）才走一次多块；否则要拆成 3 次字节模式。
- **为什么卡人**：三种错法（自增/单块/长度错）在串口上都表现为"超时"或"CFM 内容不对"，
  区分不出来。**建议 S2 阶段先做一个自检**：往 fn1 `0x10` 写 1536 字节已知图案，
  再从 `0x0F` 读回同样长度，比对一致才算 CMD53 通。

### ③ 固件下载是个"不可重入、无诊断"的状态机，错一步只能重新上电

- 下载全程没有"读状态寄存器看进度"这种东西：唯一反馈是**每块一个 CFM 消息**，
  靠 `cmd_mgr` 用 `id` 匹配（`aic_bsp_driver.c:154-176`），超时 3000 ms 后整个管理器标记
  `RWNX_CMD_MGR_STATE_CRASHED`（`:136-145`）——**之后所有命令都失败**，无法局部重试。
- 顺序还带隐藏依赖：adid/patch 必须在主固件**之前**（`aic_bsp_driver.c:1425-1428` vs `:1776`），
  patch_config 的 4 个魔法字必须在 start_app **之前**，而 `PTCH/HCTP` 的写入地址来自
  **从固件镜像里读出来的两个指针**（`0x00120198` / `0x001201A0`，`aic8800d80_compat.c:138-172`）——
  也就是说**主固件没下载对，这两次读会拿到 0 或垃圾**，然后往一个非法地址写 4 次，芯片挂死。
- 新固件（`fwver > 0x06090100`）还多一层分支：patch 表地址改成从 `0x001201A4` 读（`:162-171`）。
  Duo S 上的固件到底是哪一档，**必须先把 `0x0012001C` 读出来才知道**。
- **为什么卡人**：失败模式是"超时"，但可能的原因有 15 种以上（SDIO 宽度/时钟/CRC8/长度/对齐/地址/顺序）。
  **建议 S4 分三次上板**：第一次只读 `0x40500000`；第二次只下 `fmacfw` 并读回 `0x0012001C`；
  第三次才走完整序列。每次失败都能靠"上电复位"回到已知状态。
- 另外提醒：`buf[3]` 的 **CRC8 只有 D80 要算**（`aic_bsp_driver.c:265-267`），
  算错/不算都会被固件静默丢弃（没有 NAK、没有错误码），现象跟"卡没响应"一模一样。

---

## 8. 附录

### A. 寄存器速查

**SDHCI 标准（基址 + 偏移）**

| 偏移 | 名称 | 我们会不会用 |
|---|---|---|
| `0x04` | BLOCK_SIZE | ✅ 512 |
| `0x06` | BLOCK_COUNT | 🆕 多块必需 |
| `0x08` | ARGUMENT | ✅ |
| `0x0C` | TRANSFER_MODE | 🆕 补 `MSBS(bit5)` |
| `0x0E` | COMMAND | ✅ |
| `0x20` | BUFFER（PIO 口） | ✅ |
| `0x24` | PRESENT_STATE | ✅（注意 `CARD_INSERTED`） |
| `0x28` | HOST_CONTROL | 🆕 `bit1`=4-bit |
| `0x30` | INT_STATUS | 🆕 `bit8` = Card Interrupt |
| `0x34/0x38` | INT_ENABLE / SIGNAL_ENABLE | 🆕 `bit8` |
| `0xE8` | P_VENDOR_AREA1 | ✅ 得 `0x200` |
| `area+0x00` | MSHC_CTRL | 🆕 `bit16` |
| `area+0x40` | PHY_TX_RX_DLY | ✅ `0x01000100` |
| `area+0x4C` | PHY_CONFIG | ✅ |

**AIC func1（CMD52 地址）** 见 §2.5 表。

**TOP（`0x03000000`）**

| 偏移 | 位 | 含义 | 我们怎么用 |
|---|---|---|---|
| `0x294` | 8 / 9 | `sd1_carddet_ow` / `sd1_carddet_sw` | 🆕 都置 1 → 假装有卡 |
| `0x294` | 10 | `sd1_phy_sel` | 0 = PWR_SD1 pad（U-Boot 已设） |

**GPIO（`0x03020000`，bank A = `porta`）**

| 偏移 | 含义 |
|---|---|
| `0x00` | `SWPORT_DR`（数据；bit15 = GPIOA15） |
| `0x04` | `SWPORT_DDR`（方向；1 = 输出） |

### B. 关键行号索引

| 主题 | 文件:行 |
|---|---|
| SDIO 功能号/VID/DID | `aicsdio.c:79-89` |
| probe 把 func2 换成 func1 | `aicsdio.c:290-294` |
| D80 走 `aicwf_sdiov3_func_init` | `aicsdio.c:328-332` |
| WiFi 电源 GPIO + 50 ms | `aicsdio.c:501-521` |
| 私有寄存器表（V3） | `aicsdio.c:1694-1721`、`aicsdio.h:36-48` |
| D80 func 初始化全流程 | `aicsdio.c:1819-1898` |
| 块大小 / 使能 func | `sdio_io.c:159-183`、`:59-100` |
| 4-bit 切换 | `sdio.c:246-274`、`:355-371` |
| 读 CIS | `sdio_cis.c:246-350`、`:78-100` |
| 卡中断声明 | `sdio_irq.c:299-333` |
| D80 中断使能 | `aicsdio.c:1367-1382` |
| D80 收包 handler | `aicsdio.c:1506-1557` |
| 流控 | `aicsdio.c:693-729` |
| 发送帧长对齐 | `aicsdio.c:987-1012` |
| CRC8 | `aicsdio.c:2004-2027` |
| 下行帧头（含 CRC8 分支） | `aic_bsp_driver.c:251-283` |
| 下片段 1024 B | `aic_bsp_driver.c:1233-1280` |
| 补丁表解析/下发 | `aic_bsp_driver.c:1297-1360`、`:1469-1517` |
| D80 固件地址 | `aic_bsp_driver.h:345,356-359` |
| patch_config（PTCH/HCTP） | `aic8800d80_compat.c:114-250` |
| start_app | `aic_bsp_driver.c:1546-1562` |
| D80 init 总流程 | `aic_bsp_driver.c:1852-1950`、`:1726-1804` |
| 命令超时 | `aic_bsp_driver.h:19`、`aic_bsp_driver.c:126-146` |
| 厂商 host quirk（NO_1_8_V） | `sdhci-cv181x.c:1222-1226` |
| 厂商 sdio 复位（SD1 专用位） | `sdhci-cv181x.c:826-875` |
| 我们的现状 | `Sdhci/{Sdhci.c,Card.c,Command.c,Transfer.c,Utility.c}`、`Sdhci.h` |

### C. 留给下一轮的问题（需要真机才能答）

1. `P_VENDOR_AREA1`（`0xE8 & 0xFFF`）在 SD1 上到底读出多少？我们按 `0x200` 假设（`Card.c:25` 会打印）。
2. `TOP 0x294` 的卡检测覆盖在 SD1 上够不够（`PRESENT_STATE` 的 `CARD_STABLE` 会不会也变 1）？
3. 卡的 CCCR `0x08` 里 `SMB`（多块）是否为 1？决定 1536 字节要 1 次多块还是 3 次字节模式。
4. `fn0 0xF2 = 0x7F` 是什么？（CCCR 厂商保留区，值不明，只能照抄）
5. 固件版本 `0x0012001C > 0x06090100` 吗？决定 patch 表地址走新分支还是 `0x0016F800`。
6. 50 MHz 4-bit 下 `fmacfw` 下载实际耗时（决定 S4 是否需要先做 DMA）。
