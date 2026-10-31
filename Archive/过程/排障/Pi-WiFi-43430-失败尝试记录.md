# Pi WiFi 43430（pwifi-2）失败尝试记录

真机：Raspberry Pi 3B，BCM43430 `chip=a9a6 / rev=1`，SDIO 走 Arasan SDHCI @ `0x3F300000`。
固件：`Assets/Firmware/brcm/brcmfmac43430-sdio.bin`（A1，419798 B，7.45.98.118），首 4 字节 `0x00000000`。

本文只记「做过了、没成」的尝试与串口取证，供后续避免重复走。成功路径确认后再另立结论。

## 观察基线（已确认为正常）

- `cc before write=1541a9a6` —— 灌片前 CMD53 读 ChipCommon 正常。
- `fw bytes=00010000 … 00060000` —— 419798 B 全部灌完，无 R5 错误、无 DATA_TIMEOUT。
- `csr after write=00000049` —— `FORCE_ALP|ALP_REQ|ALPAV`，ALP 通路全程存活。
- `nvram ok, exit download` —— NVRAM 写入与长度字均成功。

结论：**F1 写通路、背板窗、ALP 请求都是好的**，问题只落在「拉 ARM 之后 HT 不上」。

## 失败尝试

### 尝试 1：固件灌完后用 CMD53 回读校验（`WifiBackplaneRead32(4)`）

- 现象：`read fail isr=00000000 pst=01ff0206`。
- 解码 PRESENT_STATE：bit1 `DATA_INHIBIT`、bit2 `DAT_LINE_ACTIVE`、bit9 `DOING_READ` 同时置位；
  `INT_STATUS=0` 表示既无错误也无 `DATA_TIMEOUT`。卡把 DAT 线拉住但不送数据，主机侧干净接收命令、数据阶段永不完成。
- 后续：`WifiSdioHostCommandDataRecover()` 清 DATA_INHIBIT 后，**下一次**写 `@00000040` 报
  `r5=00001800`（R5_ERROR，I/O 状态=CMD）+ `isr=00000010`（DATA_TIMEOUT）。
- 判定：被 abort 的 CMD53 读在卡侧背板队列里留了挂起项，主机侧 abort 只清了 `DATA_INHIBIT`，清不掉卡侧条目，后续写被拒。
- 处置：**生产路径去掉 RAM 读回校验**。对表 brcmfmac：`brcmf_sdio_verifymemory()` 在非 DEBUG 下是空函数，
  Linux 生产路径本来就不回读固件。此结论保留，不再回读 SOCRAM。

### 尝试 2：按 `brcmf_chip_set_passive` 做完整冷启动初始化（D11 复位 / SOCRAM 复位 / bankidx / bankpda）

- 做法：灌片前写 `SOCSRAM_BANKX_INDEX=3`、`SOCSRAM_BANKX_PDA=0`，并复位 D11、复位 SOCRAM。
- 现象：日志出现 `D11 reset ok` 但 HT 依旧不上；且该批步骤在本板没有带来任何状态改善。
- 判定：对表 brcmfmac，**`brcmf_chip_set_passive()` 在 SDIO 路径上根本不会被调用**
  （`brcmf_sdio_buscore_ops` 没有 `.reset` 成员）。Linux SDIO 首次下载不做 cm3_set_passive 那套。
  cyw43 的 `bankidx=3` 是 43439（4 bank）路径的做法，Linux 对 43430 不写。
- 处置：删除这些步骤，只留 `WifiCoreDisable(ARM)`。这符合 brcmfmac 的 SDIO 首次下载路径。

### 尝试 3：在 AI 核 `+0x18` 写复位向量（`WIFI_AI_RESET_VECTOR_OFFSET`）

- 动机：怀疑 CM3 出复位后不知从哪取指。
- 做法：`WifiCoreReset` 在释放复位前 `write32(WrapperBase + 0x18, 0)`。
- 现象：**串口输出与改动前逐字节一致**（仍 `arm-after io=00000001 rst=00000000`、`HT timeout csr=0x50`），零效果。
- 判定：方向错了。对表 brcmfmac，复位向量不是写在 AI 核 `+0x18`，而是 `brcmf_sdio_buscore_activate()`
  把 `rstvec = get_unaligned_le32(fw->data)` 作为 **4 字节字写到背板地址 0**，且 `rstvec > 0` 才写。
  本固件首字 `0x00000000`，Linux 直接跳过这次写。`+0x18` 也不是 `brcmf_chip_ai_resetcore` 碰的寄存器，
  属于未定义写入，风险高。
- 处置：**回退**该写。

### 尝试 4：ARM 先复位、SOCRAM 后复位（顺序颠倒）

- 现象：`arm-after io=00000001 rst=00000000` 但 `socram-after io=00000000 rst=00000001`，
  即 ARM 起、固件所在的 SOCRAM 仍在复位里，CPU 取指必然失败。
- 处置：改为 **SOCRAM 先于 ARM** 拉出复位（对表 cyw43：先 disable/reset SOCRAM，最后才 reset ARM）。
- 结果：`socram-after io=00000001 rst=00000000` + `arm-after io=00000001 rst=00000000`，两核都起来了。
  这是**有效修正**，保留。但 HT 仍未上（0x50）。

### 尝试 5：HT 时钟请求直接写 `0x10`

- 代码：`WifiSdioCommand52Write(F1, CHIPCLKCSR, WIFI_CLOCK_REQUEST_HT)`，即把 `0x10` 整个写进去。
- 串口取证：`csr after write=00000049`（`0x01|0x08|0x40`）→ `HT wait csr=00000050`（`0x10|0x40`）。
- 解码：`0x08 ALP_AVAIL_REQ` 和 `0x01 FORCE_ALP` **被这次写清掉了**，只剩 `HT_REQ|ALPAV`。
- 判定：这是硬 bug。对表 brcmfmac `brcmf_sdio_htclk()`，请求 HT 是**读-改-写**（`clkctl |= SBSDIO_HT_AVAIL_REQ`），
  从不清 ALP 位；`brcmf_sdio_buscoreprep()` 也明确要求 ALP 位全程保持。
- 处置：已改为读-改-写。
- 复测结果：`HT req csr=00000059`（`0x01|0x08|0x10|0x40`），ALP 位保住了 —— **修正是对的，但 HT 仍不上**。
  说明 ALP 位不是 HT 不上的原因，只是必要条件。

### 尝试 6：固件在 SOCRAM 仍处复位时灌入（根因）

- 现象链：`WifiEnterDownload()` 只做 `WifiCoreDisable(ARM)`，**没有把 SOCRAM 拉出复位**；
  `socram-after rst=00000001` 一直如此。也就是说 419798 字节固件是在 SOCRAM 核处于复位状态时写下的。
- 判据：背板写对一个处于复位的存储器控制器会**返回成功但不落地**。这同时解释三件事：
  1. 写全程 `fw bytes=` 无错、`csr after write=0x49` 正常；
  2. 两核最后都能拉起来（`io=1 rst=0`），固件却从不运行 → `HT_AVAIL` 永不上；
  3. 早期对 SOCRAM 的 CMD53 **读**楔死（`pst=01ff0206`，`INT_STATUS=0` 无错、DAT 线被拉住），
     正是读一个复位中的存储器控制器的表现。
- 对表 cyw43 `cyw43_ll_bus_init`：**先 disable ARM → disable/reset SOCRAM（拉出复位）→ 灌固件 → 最后 reset ARM**。
  我们的顺序是「灌固件 → 才拉 SOCRAM」，颠倒。
- 处置：`WifiEnterDownload` 增加 `WifiCoreReset(SOCRAM)`，`WifiExitDownload` 不再动 SOCRAM
  （灌完再复位 SOCRAM 会把刚写进去的固件一起清掉）。
- 复测结果：`SOCRAM up ok` 出现，但固件写在 **@00000040** 就失败：
  ```
  fw write fail @00000040
  fw pst=01ff0506
  fw isr=00000010
  fw r5=00001800
  ```
- 解读（重要，说明前一阶段的"全绿"是假象）：
  - SOCRAM 在复位时，写背板返回成功但零落地、也没有任何超时 —— 所以此前 `fw bytes=` 一路到 `00060000` 全是假通过。
  - SOCRAM 上电后，写才真正打进 SRAM，**立刻暴露出真实的传输问题**：第一条 64B 成功，第二条就失败。
  - `pst=01ff0506`：`DATA_INHIBIT(0x2) + DAT_LINE_ACTIVE(0x4) + WRITE_TRANSFER_ACTIVE(0x100) + BUFFER_WRITE_ENABLE(0x400)`。
  - `isr=0x10`：`DATA_TIMEOUT` —— 卡没在时限内回 CRC 状态。
  - `r5=0x1800`：bit11 `ERROR`=1，bits13:12=`01` 即 `IO_CURRENT_STATE=CMD`，**`OUT_OF_RANGE(0x200)`=0**
    → 地址没错，是**卡侧 function busy**，卡拒绝接受数据阶段。

### 尝试 7：不等卡释放 DAT0 就连发 CMD53（根因二）

- 判定：写块结束后卡在 DAT0 上拉 busy（SRAM 实写有延迟），主机不等线空就发下一条 CMD53。
  对表 Linux `mmc_io_rw_extended`：卡 busy 时应**阻塞等待**，而不是当失败返回。
- 处置：新增 `WifiSdioWaitCardIdle()`（轮询 `PRESENT_STATE` bit2 `DAT_LINE_ACTIVE` 直到为 0），
  在每条 CMD53 写之前等待；R5 若只置 `ERROR`（无 `OUT_OF_RANGE`/CRC）判为瞬态 busy，
  等线空后重发（最多 4 次），其余错仍立即失败。
- 状态：**已改，待真机验证**。

## 待验证假设（下一刀）

若 SOCRAM 先上电后 HT 仍不上，则怀疑固件版本 A0/A1：

- 现有：`brcmfmac43430-sdio.bin`（A1，7.45.98.118）。
- 备选：`brcmfmac43430a0-sdio.bin`（A0，382455 B，7.13.53.9）。
- 已知流言：`HT Avail timeout ... clkctl 0x50` 常对应 A0/A1 固件选错。
- 对表 brcmfmac 的 `brcmf_sdio_fwnames[]`：`chiprev == 0` 取 a0，`>= 1` 取 a1。本板 rev=1 取 a1 是对的，
  但**芯片 ID 寄存器 `0x1541a9a6` 的高 16 位仍需确认解读**（`0x1541` 究竟是 chiprev 还是别的编码）。
  这一条只能靠实切固件来验。

### 尝试 8：想用 CMD52 把「地址不可达」和「CMD53 数据阶段坏」分开

- 做法：给 F1 的背板字节访问加 CMD52 探针，认为 CMD52 无数据阶段、失败不锁卡，一次启动可扫一片。
- 现象：`c52 @18004000 rd=fail`、`c52 @00000000 rd=fail`，两个都失败。
- 判定：**探针写错了**。CMD52 的地址里也 OR 了 `0x8000`（`SBSDIO_SB_ACCESS_2_4B_FLAG`），
  该标志只用于 CMD53 32 位访问；CMD52 走裸地址（对表 `brcmf_sdiod_readb` → `sdio_readb` → 裸地址）。
- 教训：同一个常量在不同命令里语义不同，不能因为「是背板访问」就套同一套地址拼装。

### 尝试 9：去掉标志后逐点扫背板

- 现象：`c52 @18004000 rd=00000040`（= `0x0005a040` 的最低字节，**CMD52 到背板数据区是通的**），
  但 `c52 @00000000 rd=fail`、`byte wr @0 fail`。
- 判定：背板地址 0 是真不可访问，与 brcmfmac 的 `rambase = 0` 冲突。
- 冲突来源确认：`brcmf_chip_get_raminfo()` 的 `BCMA_CORE_INTERNAL_MEM` 分支
  **从不给 `rambase` 赋值**，`brcmf_chip_priv` 由 `kzalloc` 分配 → `rambase = 0` 是确定的，
  不是我们读错源码。

### 尝试 10：一次启动扫 12 个地址

- 现象：12 个点**全 fail**，包括上一轮读到 `0x40` 的 `18004000`。
- 判定：第一次坏址访问（`@00000000`）**把整个 F1 废掉**，之后连窗口寄存器都写不进去，
  于是后续每一点都在「窗口设置失败」上返回，看起来像「全不可达」。
- 佐证：紧接着做的「好→坏→好」粘连测试，第三点 `C @18004000 rd=fail`，
  而同一个地址在踩坏点之前是 `rd=00000040`。
- 规则：**一张卡一次启动只能碰一个可疑地址**；坏址之后的任何结论都不可信。
  更根本的办法是不要再猜地址——去读 EROM。

### 尝试 11：读 EROM 核表（进行中）

- `cc before write=1541a9a6`（chip id 低 16 位 `0xa9a6`）。
- `erom ptr=18108000`，前 12 个描述符已解出：
  - `4bf80001` → 组件 part `0x800` ChipCommon，地址描述符 `18000005` → `0x18000000`，wrapper `181000c5` → `0x18100000`
  - `4bf81201` → 组件 part `0x812` D11，地址 `18001005` → `0x18001000`，wrapper `181010c5` → `0x18101000`
  - `4bf82901` → 组件 part `0x829` SDIO
- 截断在 12 个字，**ARM CM3（`0x82a`）和内部存储器（`0x80e`）的地址描述符在表尾**，尚未拿到。
- 下一步：读到类型 `0xF` 表尾为止，从地址描述符直接取存储器 aperture，不再靠猜。

### 尝试 11 结果：EROM 全表解出，SRAM 地址确认

`erom ptr=18108000`，41 个描述符。按 `type = word & 0xF`、`part = (word >> 8) & 0xFFF` 解：

| 组件 | part | 寄存器基址 | wrapper |
|---|---|---|---|
| ChipCommon | `0x800` | `0x18000000` | `0x18100000` |
| D11 (WLAN) | `0x812` | `0x18001000` | `0x18101000` |
| SDIO | `0x829` | `0x18002000` | `0x18102000` |
| ARM CM3 | `0x82a` | `0x18003000` | `0x18103000` |
| 内部存储器 | `0x80e` | `0x18004000` | — |

存储器核后面跟三组「地址 + 尺寸」描述符：

```
00000135 00080000    → 0x00000000, 0x80000 (512 KB)
10000135 00080000    → 0x10000000, 0x80000 (512 KB)
1e000135 000a0000    → 0x1e000000, 0xa0000 (640 KB)
```

结论：**`rambase = 0`、512 KB 都正确**，与 `brcmfmac` 的 kzalloc 默认值吻合，也与 `WIFI_RAM_SIZE` 吻合。
此前「地址 0 不可访问」的判断作废——那是 CMD52 字节访问路径拒绝，不是 CMD53 拒收。

### 关键进展：第一块 64 字节写进去了

SOCRAM 出复位后，`fw write fail @00000040`——失败点从「第一块」后移到「第二块」。
说明地址 0 经 CMD53 可写，前 64 字节被卡接收。

失败时状态：

```
pst=01ff0506   DATA_INHIBIT | DAT_LINE_ACTIVE | WRITE_TRANSFER_ACTIVE | BUFFER_WRITE_ENABLE
isr=00000010   Buffer Write Ready（正常）
r5 =00001800   ERROR | 0x1000
```

主机 64 字节已进 FIFO，卡始终不消费：DAT0 一直忙，FIFO 排不空，DATA_END 等不到。
即**卡侧的背板写没有完成**，不是主机没发出去。

### 下一个检查：SRAM 到底能不能读

之前从未用 CMD53 读过 SRAM。读通 → SRAM 活着、故障在写路径（块模式 vs 字节模式）；
读不通 → 该 aperture 对 SDIO 从端口无响应，需要换 `0x10000000` 视图。

### 尝试 12：Linux 地面真值 + 两个真实缺口

拿到能跑 Linux 的卡，读到 brcmfmac 的原始日志：

```
brcmf_chip_get_raminfo RAM: base=0x0 size=524288 (0x80000) sr=65536 (0x10000)
using brcm/brcmfmac43430-sdio for chip BCM43430/1
F1 signature read @0x18000000=0x1541a9a6
```

地址 0、512 KB、`a9a6` 签名，与我们的实现逐项吻合，所以地址/尺寸/枚举都不是问题。

**先证伪自己的一条思路。** 我曾认定 SRAM 在 HT 域、HT 没起来所以访问不响应。源码否掉了：

```c
	bus->alp_only = true;
	err = brcmf_sdio_download_firmware(bus, code, nvram, nvram_len);
	if (err) goto fail;
	bus->alp_only = false;
```

`alp_only` 就包在下载外层，`htclk` 里 `clkreq = alp_only ? ALP_AVAIL_REQ : HT_AVAIL_REQ`
——**下载固件全程只要 ALP，HT 是下完固件之后才要的**。我们实测 `csr=0x50`（ALP_AVAIL 已置）
正是 Linux 下载期的状态。HT 是红鲱鱼，白改了一轮。

**缺口一：43430 专有的 bank 3 重映射。** `brcmf_chip_cm3_set_passive()` 里：

```c
	core = brcmf_chip_get_core(&chip->pub, BCMA_CORE_INTERNAL_MEM);
	brcmf_chip_resetcore(core, 0, 0, 0);
	if (chip->pub.chip == BRCM_CC_43430_CHIP_ID ||
	    chip->pub.chip == CY_CC_43439_CHIP_ID) {
		brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankidx), 3);
		brcmf_chip_core_write32(sr, SOCRAMREGOFFS(bankpda), 0);
	}
```

结构体偏移 `bankidx` = 0x10、`bankpda` = 0x44，落在 SOCRAM 寄存器块 `0x18004000`。
我们的 `Private.h` 里 `WIFI_SOCRAM_BANK_INDEX` / `WIFI_SOCRAM_BANK_PDA` 两个常量**早就定义好，
但从来没有任何代码使用**（`grep` 结果：四个常量全未使用）。

此前代码注释写着「bankidx/bankpda 是 43439 做法，SDIO 路径不做」——错的，源码把它和 43430 并列。
没关重映射，主机访问 SRAM 的地址会被搬走，正好解释「aperture 在、读不响应、写把卡挂住」。

**缺口二：`buscoreprep` 的时钟最终态。** 完整序列：

```c
clkset = FORCE_HW_CLKREQ_OFF | ALP_AVAIL_REQ;   // 0x28
writeb(CHIPCLKCSR, clkset);
if ((readb() & ~SBSDIO_AVBITS) != clkset) return -EACCES;   // 回读逐位校验
SPINWAIT(ALP_AVAIL, PMU_MAX_TRANSITION_DLY);                 // 最多 15 ms
clkset = FORCE_HW_CLKREQ_OFF | FORCE_ALP;       // 0x21，FORCE_ALP 必须留
writeb(CHIPCLKCSR, clkset);
udelay(65);
writeb(SBSDIO_FUNC1_SDIOPULLUP, 0);              // 0x1000F，关多余上拉
```

我们原来写的是 `0x29`（多带 `FORCE_ALP` 却没走到 `0x21`），且从没写过 `SDIOPULLUP`。

**本轮改动**：`WifiAlpRequest` 逐字对齐 `buscoreprep`；`WifiEnterDownload` 补 D11 带 PHY 位复位
（`0x000C` / `0x0004` / `0x0004`）与 bank 3 重映射关闭；下载前的 HT 等待删除（改回下完再要）。

### 失败 13：以为「块长读 0x100」= 卡不支持块模式

`WifiSdioReadFunctionMaxBlockSize()` 读 F0 `0x100/0x101` 恒得 `00000000`，于是断定卡不实现块长，
退回字节模式。**这是读错了寄存器**：FBR 里 `0x00` 是 CSA 指针，`0x10/0x11` 才是块长。
实测 `0x110/0x111` 写 64 后读回 `00000040`，写 512 后读回 `00000200`——FBR 一直可写。

### 失败 14：块模式只发 1 块

`Blocks = 1` 时 `TransferMode = 0`（既无 MULTI 也无 BCK_CNT_EN），数据阶段永不收尾，
`r5=00001000`、`pst=01ef0006`。这是块模式里最不典型的角落，卡不认账不等于块模式不可用。

### 失败 15：字节模式计数写 512

```c
Argument = ... | (ByteCount & 0x1FF);
```

计数字段只有 9 位（0–511），`512 & 0x1FF = 0`，等于发了一包「0 字节」，
卡回 `r5=00001100`，bit8 = `OUT_OF_RANGE`。块长改成 512 之后的所有 `OUT_OF_RANGE` 都是这个 bug，
不是卡的问题。

### 失败 16：以为等待窗口太短

把 DAT0 等待从「无延时自旋几毫秒」改成墙钟 500 ms、再到 3000 ms，并打印实测耗时。
结果 `dat0 idle ms=00000000`（寄存器写）与 `dat0 busy over 3000ms`（阵列写），
**证明卡不是慢，是根本不应答**。时间维度排除。

### 失败 17：换内存别名（0x0 / 0x10000000 / 0x1e000000）

EROM 给同一块 SRAM 列了三个从端口别名（`stype = SLAVE`、尺寸 512K/512K/640K）。
三个别名表现**逐字节相同**：都是 `fifo stall word=00000030`、`fw write fail @<base>`。
地址维度排除。

### 失败 18：SOCRAM 复位 A/B

去掉 `WifiCoreReset(SOCRAM)` 后实测 `socram io=00000000 rst=00000001 core=00000000`，
连 `0x18004010` 都写不进去（`bank remap off fail`）。**证明那次复位是必须的**，
且复位后 `io=00000001 / rst=00000000` 与 Linux `resetcore` 终态 `postreset | CLK` 一致。

### 失败 19：`FORCE_ALP | FORCE_HW_CLKREQ_OFF` 一直留着

`buscoreprep` 末尾写 `0x21`，但紧接着 `brcmf_sdio_download_firmware` 会调
`clkctl(CLK_AVAIL)`，而 `brcmf_sdio_htclk(on)` 是**整字节覆写**成 `ALP_AVAIL_REQ`（`0x08`）：

```c
clkreq = bus->alp_only ? SBSDIO_ALP_AVAIL_REQ : SBSDIO_HT_AVAIL_REQ;
brcmf_sdiod_writeb(..., SBSDIO_FUNC1_CHIPCLKCSR, clkreq, &err);
```

等于 Linux 灌固件时 `CHIPCLKCSR = 0x48`（ALP_AVAIL 置位、两个强制位清零），
我们停在 `0x61`。补上后 `csr=00000048`，ALP 保住了——但阵列写仍然挂住。

### 失败 20（本轮结论）：4 字节写就挂，与大块无关

在固件写入之前插最小粒度探针：往 `base+0x100` 写 `0xBEEF1234`，再读回。实测：

```
Boot: Pi WIFI probe start
Boot: Pi WIFI no data end words=00000001
Boot: Pi WIFI sram write32 fail
```

**单个 4 字节写的数据阶段就没有 DATA_END**，而同一颗核的寄存器写
（`0x18004010`、`0x18004044`、`0x18004100`）全部瞬时完成。

## 阶段结论（03:15）：把「链路」与「目标」分开

已验证与 Linux 逐位一致的部分（这些不用再查）：

| 项目 | 证据 |
|---|---|
| 芯片识别 | `0x1541a9a6`，BCM43430/1 |
| 背板窗口寄存器 | F1 `0x1000A/B/C`，回读与写入一致 |
| 窗口粒度/偏移 | `SBWINDOW_MASK=0xFFFF8000`、`OFT_MASK=0x7FFF`，与 Linux 相同 |
| 32 位访问标志 | `0x8000`，Linux `ramrw` 同样 OR 上 |
| 传输形状 | 一次 32 KB、511 块 × 64、余数走字节模式（照 `sdio_io_rw_ext_helper`） |
| 块长 | F1 上限 64（512 被 R5 `OUT_OF_RANGE` 拒） |
| 时钟 | `csr=0x48`，ALP_AVAIL 在位，两强制位已清 |
| 存储核状态 | `io=0x01`、`rst=0`、4 bank × 128 KB = 512 KB，与 Linux `size=524288` 一致 |

唯一未解释的事实：**同一颗存储核，寄存器块可读写，存储阵列的写永不完成。**

下一步的三个候选（都只需一次启动）：

1. **复位的是不是正确的包装器。** EROM 里 `18104185` 紧随存储核，我们据此用 `0x18104000`。
   但 EROM 还列了 `18105085`/`18106085`/`18107005`/`18108005`/`1810a035`，
   若存储核真正的包装器是其中另一个，我们一直在复位别人——寄存器块直连背板仍会应答，
   而阵列的使能位没人打开，正好是这个现象。
2. **PMU 侧 SRAM 电源域。** `bwalloc`（`0x18004004`）读出 `e5e5e5e5` 这种填充值，
   说明该核有些寄存器不实现；若阵列供电由 chipcommon PMU 控制，需要显式打开。
3. **ARM 是否真的停在复位里。** `resetcore(0,0,0)` 会清 `RESET_CTL`，CM3 可能从地址 0
   取指跑飞；虽然 Linux 同样处理，但值得用「保持 `RESET_CTL=1`」做一次 A/B。

### 失败 21：以为是烧错了镜像（流程事故，值得记）

有两次日志「探针代码完全没输出」，原因不是代码没执行，而是 `prepare-pi-sd` 在 build 之前跑了，
SD 卡里还是上一版 ELF。判据是我加的构建标记 `tag=`：

```
Boot: Pi WIFI load begin tag=00000111
```

此后每次改版 `WIFI_BUILD_TAG` 自增，日志第一屏就能确认板上跑的是哪一版。
**教训**：跨机器（编译机 → 树莓派 → 板子）的调试，必须有一个一眼可辨的版本戳。

### 失败 22：改下载基址到 0x1e000000（第三个别名）

EROM 给同一块 SRAM 列了三个从端口别名，第三个是 640 KB（`000a0000`）。
结果与 `0x0`、`0x10000000` **逐字节相同**：`fifo stall word=00000030`、`fw write fail @1e000000`。
地址维度彻底排除。

### 失败 23：重解 EROM 描述符，怀疑复位了错的包装器（结论：没怀疑错）

把 `brcmf_chip.c` 的位定义抠出来逐字解：

```
DMP_SLAVE_ADDR_BASE 0xFFFFF000   DMP_SLAVE_TYPE 0xC0 (SLAVE=0 SWRAP=2 MWRAP=3)
DMP_SLAVE_SIZE_TYPE 0x30 (0=4K 1=8K 3=尺寸描述符)
```

| 描述符 | 解码 |
|---|---|
| `4bf80e01` | 组件 id=0x80e（存储核），rev=0x4b |
| `18004005` | regbase `0x18004000`，SLAVE/4K |
| `00000135` + `00080000` | 别名 `0x00000000`，512 KB |
| `10000135` + `00080000` | 别名 `0x10000000`，512 KB |
| `1e000135` + `000a0000` | 别名 `0x1e000000`，640 KB |
| `18104185` | wrapbase `0x18104000`，SWRAP/4K |

按 `brcmf_chip_dmp_get_regaddr` 的算法，`0x18104000` 就是存储核自己的包装器。
**这一条排查没白做，但结论是「我们没做错」**，等于关掉一条支线。

### 失败 24：怀疑窗口高位没锁存

所有成功的访问窗口 HIGH 都是 `0x18`，三个内存别名的 HIGH 是 `00/10/1e`，所以怀疑只写进得去低两字节。
直接读回 `SBADDRLOW/MID/HIGH`：

```
Boot: Pi WIFI window lo/mid/high=00/00/1e
```

**高位确实锁存了**，同时读到 `devctl=0x00`（背板不在复位，`(devctl>>4)&3 == 0`）、
`sleep=0x03`（KSO=1、DEVON=1，设备醒着）。窗口与复位维度排除。

### 失败 25：怀疑高速时钟域没开

显式写 `HT_AVAIL_REQ`（`0x10`）并轮询 1.5 秒：

```
Boot: Pi WIFI HT before sram csr=00000050
```

bit4（请求在位）为 1、bit7（`HT_AVAIL`）始终不出现。但这**在下载期是正常的**：
HT 的 PLL 要等固件自己配置，Linux 下载期也只用 ALP（`bus->alp_only = true`）。
所以这不是原因，反而印证了「SRAM 必须挂在 ALP 上也能访问」。

### 失败 26：怀疑 ARM CM3 跑飞占死存储总线

`brcmf_chip_ai_resetcore` 末尾会清 `RESET_CTL`，等于把 CM3 从地址 0 放出来取指，
而此刻 SRAM 内容未定义。于是把 ARM 重新按回复位再试：

```
Boot: Pi WIFI ARM held in reset
```

阵列访问**照样挂**。ARM 维度排除（顺带发现持复位后启动更快，仅此而已）。

### 失败 27：反过来验证「寄存器写真的落地」

这一条推翻了我自己长期依赖的前提，所以单独记。
窗口寄存器属于 F1 空间（CMD52 直写），它能落地**不代表背板写也能落地**。
于是往可回读的 `bankidx`（`0x18004010`）写 1 读回、写 3 读回：

```
Boot: Pi WIFI bankidx write1 read=00000001 write3 read=00000003
```

**背板写确实落地。** 至此「寄存器通、存储阵列不通」从推测变成被回读证实的事实：
主机侧、卡侧、窗口、写通路、时钟全部合格，唯独阵列这个目标不应答。

### 失败 28：以为阵列读能通，只有写不通

把探针改成「先读、再写读回」（此前那次读测试是在 D11 复位/bank 重映射/ALP 全修好之前做的，不算数）：

```
Boot: Pi WIFI sram read32 fail
```

读也失败，而且**这次是立刻失败、没把卡楔死**（后面的 CMD53 还能发）。这是新现象：
以前是「没人应答」，现在是「有人应答并拒绝」。

### 失败 29：读失败到底卡在哪一层（补上被吞掉的证据）

`WifiSdioCommand53Read` 有三处静默 `return -1`，日志里分不清。补上统一上报后：

```
Boot: Pi WIFI sb read no-data r5=00001000 isr=00000000 pst=01ff0206
```

解读：

- `r5 = 0x1000` → **没有任何错误位**（bit15 CRC、bit14 ILLEGAL、bit11 ERROR、bit8 OUT_OF_RANGE 全为 0），
  bits13:12 的 `IO_CURRENT_STATE` = 1，卡是清醒的、接受了命令。
- `pst` bit1 `DATA_INHIBIT`=1、bit2 `DAT_LINE_ACTIVE`=1、bit9 `READ_TRANSFER_ACTIVE`=1
  → 卡已经拉住数据线、进入读传输态。
- `isr = 0` → 一个字节都没送来。

**读写两个方向完全对称**：写是「接受数据后卡在自己内部」，读是「接受命令后卡在自己内部」，
两者都停在「SDIO 核 → 存储阵列」这一段。

### 失败 30：强制时钟（`FORCE_ALP | FORCE_HT`）

此前只会「请求」HT（被拒），没试过**强制位**绕过请求逻辑：

```c
WifiSdioCommand52Write(SDIO_FUNCTION_1, SDIO_CHIP_CLOCK_CONTROL, 0x03);  /* FORCE_ALP|FORCE_HT */
```

结果见下一轮日志（`force clk csr=`）。若 HT 仍不出现（只有 `0x43`），说明 PLL 从未启动，
那么「SRAM 挂在 ALP 上」就成立，剩下唯一没穷举的地方是**包装器 `IOCTL` 的逐位 A/B**。

## 当前状态（2026-10-29 03:45）

已用证据排除：芯片识别、背板窗口寄存器与粒度、32 位访问标志、传输形状（32 KB / 511 块 × 64 / 余数字节）、
块长上限、时钟最终态、SOCRAM 复位、bank 重映射、ARM 状态、背板复位位、KSO 睡眠、三个内存别名、
背板写是否落地、读是否通。

**唯一未解释的事实**：同一颗存储核，寄存器块可读可写可回读，存储阵列的读和写都停在卡内部。

下一步（按优先级）：

1. 包装器 `IOCTL` 逐位 A/B（唯一没穷举的地方）。
2. 若 HT 强制也无效，查 PLL/PMU 是否需在下载前显式启动。
3. 兜底方案：用 yocto/`brcmfmac` 抓一次完整下载过程的 SDIO 命令序列做逐条比对（成本高，但能给出确定的差异）。

## 失败 31：电池式快照 + 自动选基址（tag 121）

一次启动把 SOCRAM 可能门控位与五个包装器 IOCTL 全部打出来，并依次试读三个别名、
哪个通就用哪个当下载基址。结果：

```
Boot: Pi WIFI lpo ctl=00000291 div=00249f00
Boot: Pi WIFI memregs e5e5e5e5 00000000 20ffffff 00000011
Boot: Pi WIFI ioctl 00000001 00000005 00000001 00000000 00000001
Boot: Pi WIFI bwalloc set0 read=00000000 standby set0 read=00000000
Boot: Pi WIFI sram base no=00000000
Boot: Pi WIFI sram base no=10000000
Boot: Pi WIFI sram base no=1e000000
```

- LPO 正常：`ENAB|BUSY`、DIVI=585、DIVF=3840 → 32.768 kHz。
- 五个包装器 IOCTL 全部符合预期（arm=0 是因为它被按在复位里）。
- `bwalloc`、`standbyctrl` 都可写、已置 0，**无关**。

**同时纠正两个偏移错误**（我此前算错，害得解读跑偏一轮）：

```c
struct sbsocramregs 逐字偏移
0x00 coreinfo   0x04 bwalloc    0x08 extracoreinfo  0x0C biststat
0x10 bankidx    0x14 standbyctrl
0x40 bankinfo   0x44 bankpda
0x80 extmemconfig …
0x1E4 workaround  0x1E8 pwrctl   0x1EC 落在 PAD[133]（未实现）
```

所以真值是 `workaround(0x1E4)=0x000000ff`、`pwrctl(0x1E8)=0x00000011`；
此前打出来的 `0x1EC` 是填充区，读它会**命令级失败**。

## 失败 32：强制真 POR（tag 122）

怀疑 Pi 固件早已给 WiFi 加电，我们写 `WL_ON=1` 不产生上电复位，芯片 ROM 的存储域上电从未跑过。
改成断开 100 ms 再上电、等 500 ms：

```
Boot: Pi WIFI WL_ON cycle
Boot: Pi WIFI memregs e5e5e5e5 00000000 20ffffff 000000ff 00000011
Boot: Pi WIFI sram base no=00000000 / 10000000 / 1e000000
```

**仍然三个全 no。** 上电时序不是原因。

## 本轮最有价值的一个观察（成败分界）

同一块卡上，两种失败方式完全不同：

| 目标 | 现象 | 含义 |
|---|---|---|
| `0x180041EC`（未实现寄存器） | `sb read cmd` 失败 | 卡**连命令都不应答** |
| `0x00000000` / `0x10000000` / `0x1e000000` | `sb read no-data`，`r5=0x1000`（无错误位），`pst` 里 `DAT_LINE_ACTIVE=1` | 卡**接受命令、进入了传输态、然后一个字节都不发** |

结论：三个内存别名在卡的地址解码里**是存在的**，事务被接受后卡在自己内部卡住——
不是地址错、不是协议错、不是波形错，而是那段数据通路没有被启用。

## 下一步（不再依赖反复烧卡）

1. 在树莓派 Linux 侧读真值：内核模块通过 `sdio_memcpy_fromio` 走**同一套窗口机制**，
   读 `bwalloc / workaround / pwrctl / standbyctrl` 与三个别名，和我们的数值逐一对齐。
2. 若数值一致，则抓 Linux 驱动实际的 CMD52/CMD53 序列做逐条比对
   （`CONFIG_KPROBE_EVENTS` 未开、`CONFIG_MMC_DEBUG` 未开，需自建模块或用 ftrace 其他入口）。

## 失败 33：在 Linux 侧复现（未完成，已暂存）

思路：Linux 在同一颗芯片上能写 SRAM，那我就在 Linux 里用「最小步骤」重做一遍。
能读通 → 它那几步就是答案；读不通 → 拿到一个 30 秒重启、不用烧卡的试验台。

做法：屏蔽 `brcmfmac` 自动加载（`modprobe.blacklist=brcmfmac,brcmfmac_cyw`），
写一个只做「ALP 握手 + 读背板」的 SDIO 驱动模块（`sdio_driver` + 通配 id_table）。

**踩到的坑（值得记住）**：第一版模块写完 `CHIPCLKCSR=0x28` 只 `msleep(20)` 就直接读背板，
没有轮询 `ALP_AVAIL(0x40)`。结果第一次背板访问把卡挂死，内核跟着挂，
systemd 看门狗把板子重启了。**ALP 没就绪时访问背板 = 硬挂**，这条在 Linux 上被独立验证了一次。

修正版加了：轮询 `ALP_AVAIL` 最多 500 ms（等不到就停止，绝不碰背板）、
每步 `kernel_write + vfs_fsync` 落盘、SRAM 访问放最后。本机交叉编译通过，但**未再执行**。

暂存原因：树莓派那台重启很慢、操作卡顿，一轮成本远超 3 分钟，投入产出不划算。

## 暂存状态（2026-10-29 08:40）

代码侧（`Kernel/HAL/Arm64/Board/rpi-3b/Wifi/`，tag `0x122`）：

- SDIO 枚举、F1 使能、块长、背板窗口读写 —— **已验证可用**
- EROM 解码、五核基址与包装器、SOCRAM 尺寸/银行 —— **已验证**
- ALP 时钟序列（`buscoreprep` 等价）、D11/SOCRAM 复位、`bankidx=3/bankpda=0` —— **已验证与 Linux 逐位一致**
- 固件/NVRAM/尺寸字的写入路径 —— 代码完成，**卡在存储阵列不可访问，从未跑通**
- 释放 ARM、HT/F2 等待、SDPCM 握手 —— 代码在位，**未验证**

唯一未解的卡点：三个内存别名（`0x0`/`0x10000000`/`0x1e000000`）对 CMD53 一律
「接受命令、进传输态、零字节返回」，R5 无错误位；而同一条路径访问 `0x18000000` 区寄存器完全正常。
已排除：窗口寄存器、ALP/HT 时钟、块模式/字节模式、两处复位顺序、ARM 保持复位、
`bwalloc`/`standbyctrl` 清零、强制真 POR、LPO 频率、固件文件与尺寸字。

若恢复推进，按此顺序（一次只动一项）：

1. 上电后等待 500 ms → 3 s（Linux 侧芯片加电到驱动接手有数秒，我们只有 0.5 s）
2. 读 Linux 的 `/sys/kernel/debug/mmc1/ios`（clock / bus width / timing spec / voltage），
   与我们按 25 MHz + default speed 的配置对照；若 Linux 是 50 MHz high-speed，改成一致再试
3. 若仍不通，回到 Linux 侧：修正版探针模块已在文档中，用它拿「裸路径是否也挂」这一个事实

---

## 短刀试探日志（Pi · Claim 后延后 FirmwareLoad · 进行中 · 2026-10-31）

> 现象主线：BOOT 仅枚举；RUNTIME Claim → `EnterDownload` → bankidx 写读回。  
> 门面：`pi-wifi-1`（tag `0x125` 形状）已绿；本表专攻 **`pi-wifi-2`（窗/寄存器同序）**。  
> 取证：`log save` → `RUNTIME.LOG`（halt 不自动 flush）。对照标准：`brcmf_sdio_buscoreprep` / `brcmf_sdio_kso_init` / `brcmf_chip_set_passive` 顺序。

| # | tag | 试探 | 结果 | 备注 |
| - | --- | ---- | ---- | ---- |
| 1 | `0x125` | Claim 后立刻 `WifiCoreDisable(ARM)`（xfer 前） | **`arm disable fail`** | 形状 `chip/rev/fn1/f2bl/kso` 已绿；失败点 = 背板写 ARM 包装器。→ 开 `pi-wifi-2` |
| 2 | `0x126` | 对表 Linux：先 KSO（`0x1001F`）、`AlpRequest`、再 ARM disable；FORCE 贯穿 | **挂死**（闪完后无串口推进） | KSO 写 **`0x01` 清了 DEVON(bit1)** → sleep；brcmfmac 是 **OR bit0 保 DEVON** |
| 3 | `0x127` | KSO = `read \| WAKE`；`AlpRequest` 仍写 `0x48`；FORCE 灌片前再清 | **`bankidx=fail`**；`csr=0x48` | DAT0 卡在 bankidx；`0x48`=REQUEST_ALP\|AVAIL，**无 FORCE_ALP**，与 buscoreprep `0x21` 不符 |
| 4 | `0x128` | `AlpRequest`→`0x21`（FORCE\|REQ）；FORCE 清挪到灌片前 | **`bankidx=fail`**；`csr=0x61` | FORCE 在仍楔死 → **根因不是 FORCE 缺位**，是 **SOCRAM 未 up 就写 bankidx**（Linux 先 `set_passive`/EnterDownload） |
| 5 | `0x129` | Claim：先读 ChipCommon；`EnterDownload` 后再 bankidx | **`kso read fail`**（未到 CC） | BOOT 仍 `kso=0x03`；Claim 距探针很久，芯片已睡。我们**先读再写**——睡着读失败即弃。Linux `kso_control(on)`：**先写 KSO=1（AOS 唤醒）再 poll KSO\|DEVON** |
| 6 | `0x12a` | recover→写 KSO=1→settle→poll | **`kso write fail r5=00002801`** | `R5_ERROR`+IO_STATE=TRN；`pst` 主机空闲。根因：`Command52Write` 把仅 ERROR 当死错连拒 10 次，settle 未跑；写前 abort 可能致 TRN |
| 7 | `0x12b` | 首写容 busy；无 abort；poll 仍用严读 | **`kso read fail r5=00002801`** | 写已过；poll 把仅 ERROR 当死错，5 次即弃，等不到 DEVON |
| 8 | `0x12c` | poll 容 busy；attempts=20000 | **`kso wake timeout=00000001`** | KSO 粘住、**DEVON 永不回**——纯 SLEEPCSR 唤醒救不了延后 Claim |
| 9 | `0x12d` | 灌片前 `BusRejoin`：WL_ON POR→host→enum→F1，再 KSO | **wifi-2 绿** | `rejoin ok`→`kso=03`→`cc=1541a9a6`→SOCRAM up→**`bankidx write1/3 读回一致`**。随后 `csr avail=48` 后 **`fw write fail @1e000000`** / `sram probe write fail`（`pst=01ef0006 isr=00108010 r5=00001000`）→交 **pi-wifi-3** |

### 短刀试探日志（Pi · pi-wifi-3 · SRAM 别名 · 进行中）

| # | tag | 试探 | 结果 | 备注 |
| - | --- | ---- | ---- | ---- |
| 1 | `0x12d` | 别名 `@1e000000` 64B 写（无读回） | **write fail / no data end** | 已否本别名作首刀；r5 干净、DAT 忙 |
| 2 | `0x12e` | 别名 **`@0`**；先 `csr avail=0x48`（清 FORCE）再 64B 写 | **write fail / no data end** | 与 `@1e` 同症 → **别名不是根因**；疑过早清 FORCE |
| 3 | `0x12f` | 保持 FORCE 再探 `@0` 64B | **同 `no data end`** | FORCE 非根因（`csr=0x61` 仍败） |
| 4 | `0x130` | `BlockWrite` 补 CMD53 bit26 地址递增 | **仍 `no data end`** | bit26 非充分条件（保留，对表 Linux 仍要） |
| 5 | `0x131` | 4B Write32/Read32 探 `@0`（bankidx 试探之后） | **`no data end words=1`** | 阵列写死；寄存器通 → 非块路径独有 |
| 6 | `0x132` | 去掉 bankidx 拨号；FORCE 下 4B 探 `@0` | **仍 `no data end`** | 拨号非根因 |
| 7 | `0x133` | 探针前 `csr←0x08`；4B `@0` | **仍 `no data end`**（`csr avail=48`） | 时钟态已对齐仍败 |
| 8 | `0x134` | CMD53 写路径 **始终 BLK_CNT_EN**（失败 14 / Linux blocks=1） | **仍 `no data end`** | 寄存器 Write32 早已通 → 主机 BLK_CNT_EN 非阵列根因 |
| 9 | `0x135` | 灌片前 `CHIPCLKCSR←0x00` 再 `←0x08`（对表 Linux ftrace，缺的 CLK_NONE） | **仍 `no data end`**（`csr avail=48`） | 终态与直写 0x08 相同；CLK_NONE 非根因 |
| 10 | `0x136` | 对表 `probe_attach`：`CCCR 0xF1 |= WLANRESET` + `PMUControl \| = RES_RELOAD`（ftrace 灌片前有） | **仍 `no data end`**；`cardctrl=03` `pmu ctl=01774180` | 两笔都落地；非阵列根因 |
| 11 | `0x137` | 探针前 SOCRAM IOCTL=`FGC\|CLK`；打满 r5/pst/isr | **仍挂**；`ioctl=3`；`r5=1000 isr=00108010 pst=01ef0006` | **isr=DTO\|ERROR\|BUF_WR**——硬件数据超时，非纯软件等；FGC 否证 |
| 12 | `0x138` | 读 PMU；**只** `max_res_mask←ffffffff` | **仍 DTO**；`min=res=0xfcafd77` `max` 原 `0xfcefd77` | max 只抬天花板；多出的 bit18=`0x40000` 未因写 max 而点亮 |
| 13 | `0x139` | **`min_res_mask←ffffffff`**（先 max 再 min），短等 `res_stat`，再探 | **仍 DTO**；`min set=ffffffff` `res=7fffffff` | 资源已大部点亮仍挂 →「只缺 PMU 位」不够 |
| 14 | `0x13a` | 资源拉齐后 **再 SOCRAM CoreReset + bankpda**，读回 pda，再探 | **✅ `sram probe=yes`**；`re io=1 rst=0 pda=0`；`wifi-3 stop before full fw` | **根因：SOCRAM 在 PMU 资源未齐时复位 → 寄存器通、阵列 DTO**；须先 min/max 拉资源再 CoreReset |

### 已否证（勿再试）

- Claim 时 **先读成功才允许写 KSO**（睡着必挂；应用 Linux 先写唤醒）
- 唤醒前对空闲卡做 **IO_ABORT + recover**（易 TRN + 后续 R5_ERROR）
- 用严格 `Command52Read/Write`（拒 `R5_ERROR`）当 KSO 唤醒路径——busy 会被误杀
- **仅靠 KSO poll 救延后空闲**（DEVON 不回；须 WL_ON POR + 重枚举）
- 用 `REQUEST_ALP|AVAIL`（`0x48`）顶 `buscoreprep` 的 `FORCE_ALP|REQ`（`0x21`）
- Claim 路径在 EnterDownload / SOCRAM up **之前**写 `SOCSRAM_BANKX_INDEX`
- **只换 SRAM 别名**救首块 `no data end`（`@0` 与 `@1e` 同败）
- **只靠保持 FORCE_ALP**救首块 `no data end`（`0x12f` 仍败）
- 块模式 CMD53 **不置 bit26（固定地址）**当 memcpy——对表 Linux 必须递增（已补；**单补不够**解首块）
- 在 43430 `bankidx=3/pda=0` 之后再 **拨 bankidx=1 做读回试探**（可能弄乱重映射；wifi-2 已收，wifi-3 勿再夹）
- 仅靠 `csr←0x08`（与 Linux 灌片前时钟一致）救阵列写（`0x133` 仍败）
- 仅靠 CMD53 写路径始终 `BLK_CNT_EN` 救阵列写（`0x134` 仍败；寄存器同路径已通）
- 仅靠 `CHIPCLKCSR←0x00` 再 `←0x08`（Linux CLK_NONE→AVAIL）救阵列写（`0x135` 仍败）
- 仅靠 `CARDCTRL WLANRESET` + `PMU RES_RELOAD`（probe_attach）救阵列写（`0x136` 仍败）
- 仅靠 SOCRAM IOCTL 保持 `FGC|CLK`（`0x137` 仍 DTO）
- 仅靠抬 `max_res_mask←ffffffff`（`0x138` 仍 DTO；min/res 未变）
- 仅靠 `min_res_mask←ffffffff`（`0x139`：`res=7fffffff` 仍 DTO）
- （修正）「永远禁止写 `0x01`」——Linux 唤醒首写就是 KSO=1，再等 DEVON

### 还要试多少次（估）

对照 NUC FH 短刀（40+ 才收官）：本轨是 **有 Linux 同序可抄** 的窗/寄存器刀，不是盲 DMA。

| 目标 | 估剩余短刀 | 说明 |
| --- | ---: | --- |
| **`pi-wifi-2` 绿**（寄存器同周期写读回） | **1～4** | `0x129` 若 bankidx=ok → 1 次收口；若仍 fail → 再拆「窗基 / ACCESS_2_4B / ARM wrapper 址」各 1 刀，上限约 4 |
| **`pi-wifi-3`（SRAM 别名）** | **3～8** | 历史主痛点；三别名一次只试一个；可能再碰时钟 |
| **`pi-wifi-4`～扫描可用** | **按刀估日，不按次** | 4/5/6 各有独立判据；勿与 wifi-2 次数混算 |

**本刀下一步**：板子 RUNTIME + `log save`，看 `0x129` 是否出现 `cc=` + `EnterDownload` 日志 + `bankidx` 成败。  
手测成本高 → **一次只动一项**；否证写入上表，禁止回头重试已否证项。
