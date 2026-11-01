# Duo WiFi · B2 patch 全量计划 — 独立审查（只读）

> 独立审查者，未改一行代码，产物只有本文件。
> 快照：`Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci/WifiFw.c`，1862 行，md5 `7df63c43…`（2026-10-30 11:39 采样）。
> 对照物（逐条对照，不凭印象）：
> `aic8800_bsp/aic_bsp_driver.c:1295-1352`（alloc）、`:1371-1430`（trap_data_load）、`:1469-1515`（table_load）、`aic8800d80_compat.c:114-248`（patch_config）、`aic_bsp_driver.h:265-274/425-433/495-517/541-551`、`aic8800_bsp/Makefile:29-31`；
> 表 `fw_patch_table_8800d80_u02.bin`（1048 B）——**我用 python 独立解了一遍**，不用代码注释；
> 已跑过的真机 dry 日志（只做对账，不作为正确性依据）。

---

## 0 · 结论

**现在不能跑 `wififw patch apply`。** 不是因为这 100 条写错了——它们是对的（§1）。不能跑的理由是三条 P0：

1. **独立 apply 没有"固件已驻留"前置**（§P0-1）。当前二进制里 `gAicPatchReady` 只有声明、没有赋值（`WifiFw.c:747/1841`，全仓仅此两处），所以 `write confirm` 永远拒绝 ⇒ 除非同一上电周期里跑过更早版本的下载，芯片内存里不会有 fmacfw。此时 apply 的 4 个 cfg 读读到的是 ROM/RAM 残留；`str_base` 只要不是 0 也不是 `0xffffffff`，就会拿一个野地址去写 magic（§P1-3 的判据只有这两个值）。
2. **真正该跑的 `write confirm` 全链路，其应用点存在"短读半张表"**（§P0-2）——正是"日志全绿、芯片起不来"的原产地。
3. **表解析的长度/尾部校验可被一个损坏的表文件绕过**（§P0-3），而这张表就放在可写的 FAT 分区上。

**现在可以跑的**：`wififw patch dry`（纯文件 + 纯解析，一个字节不写、一条消息不发）。它已经是对的，我的独立复算与它逐点吻合。

**必答问题里的四项关键结论是"对"**：表解析字节走法一致（差别只在尾部严格性，§P0-3）；MODE 9 个值逐项正确、没有错位一格（§1.3）；条目顺序与厂商循环一致、`udelay` 位置正确（§1.4）；100 条地址全部 4 对齐、没有一条落进主固件区 `[0x00120000,0x00170E54)`（§1.2）。

**最小推进路径**：修 P0-3 → P0-2 → P1-2/P1-3 → P1-1/P1-6 → P0-1/P1-4，最后才允许给 `gAicPatchReady` 置位、走 `write confirm`（§7）。

---

## 1 · 我独立复算了什么

### 1.1 表（自己解，不看代码注释）

tag 16 B = `AICBT_PT_TAG\0\0\0\0`，之后每条记录 = `name[16] + type(u32) + len(u32) + data[len*8]`，文件正好在 0x418 结束：

| # | name | type | len | data @ | 我的解读 |
|---|---|---|---|---|---|
| 1 | `AICBT_PINF_T` | 0 | 4 | 0x028 | 4 对（adid_addrinf/addr_adid、patch_addrinf/addr_patch、reset、adid_flag） |
| 2 | `AICBT_TRAP_T` | 1 | 19 | 0x060 | 19 对 |
| 3 | `AICBT_PATCH_TB4` | 2 | 45 | 0x110 | 45 对 |
| 4 | `AICBT_MODE_T` | 3 | 9 | 0x290 | 9 对（BT 配置块） |
| 5 | `AICBT_POWER_ON` | 4 | 3 | 0x2F0 | 3 对 |
| 6 | `AICBT_PATCH_TAF` | 5 | 20 | 0x320 | 20 对 |
| 7 | `AICBT_VER_INFO` | 6 | 8 | 0x3D8 | 字符串 `- Mar 07 2024 14:29:05 - git f94a3e4` |

合计 100 对 = 100 次写（91 mem + 9 cfg），与 dry 日志 `0065 / 0064 / 005b / 0009` 一致。

### 1.2 用 C 的算法把计划复算一遍

我按 `AicPatchBuildPlan` 的控制流（tag 跳过、`type>=1000 → len=0`、VER 跳过、PWRON 后插 delay）在 python 里重跑同一份字节：**101 项 / 100 写 / 91+9**，`per type inf=4 trap=19 tb4=45 mode=9 pwron=3 taf=20 (+delay=1)`，delay 落在下标 80（PWRON 的 77/78/79 之后），与真机 dry 逐点相同。

地址体检（全部 100 条）：

- 全部 4 字节对齐；无一条落进 `[0x00120000, 0x00170E54)`（fmacfw 331348 B）；
- 0x4050/0x4068/0x4069 的 14 条是 PWRON + TRAP 特意要打的外设寄存器（厂商同样写）；
- 关键落点属于**镜像内部**：INF 的 `0x1E7290/0x1E7294` 在 patch 镜像 `[0x1E0000,0x1E7558)` 里；TB4 的 `0x201B38…0x201FB8`、MODE 的 `0x201FC4…0x201FE8` 在 adid 镜像 `[0x201940,0x201FEC)` 里。**这决定了这些写必须发生在上传之后**（§5-2）。

### 1.3 MODE 的 9 个值（重点：有没有再错位一格）

厂商 `aic_bsp_driver.c:1481-1493` 的 `data` 是 `u32*`，一对 = `data[0]=addr, data[1]=val`，所以 `*(data+1)` 是 **pair 0 的 val**，`*(data+17)` 是 **pair 8 的 val**；pair 下标 = (字偏移−1)/2，即 0..8。按此推导，代码 `WifiFw.c:988-999` 的 `switch (i)` 逐项对应：

| pair | 地址 | 厂商表达式 | 值 | 代码 |
|---|---|---|---|---|
| 0 | 0x201FD4 | `hwinfo < 0` | 1 | case 0 ✓ |
| 1 | 0x201FD8 | `hwinfo` | 0xFFFFFFFF | case 1 ✓ |
| 2 | 0x201FD0 | `chipid==DC ? cpmode : 0` | 0 | case 2 ✓ |
| 3 | 0x201FC8 | `btmode` | 5 (BT_ONLY_COANT) | case 3 ✓ |
| 4 | 0x201FC4 | `btport` | 2 (UART) | case 4 ✓ |
| 5 | 0x201FE8 | `uart_baud` | 1500000 | case 5 ✓ |
| 6 | 0x201FE4 | `uart_flowctrl` | 1 | case 6 ✓ |
| 7 | 0x201FDC | `lpm_enable` | 0 | case 7 ✓ |
| 8 | 0x201FE0 | `txpwr_lvl` | 0x6F2F | case 8 ✓ |

交叉验证：`aic_bsp_driver.h:505-517` 的默认值 + `Makefile:31 CONFIG_SDIO_BT=n`（→ BTPORT_UART=2）与上表逐项相同；真机 dry 的 `patch[68..76]` 也与上表逐个相同。**没有错位。**

### 1.4 顺序

计划顺序 = 表文件顺序 = 厂商 `for (p = head; …)` 顺序（`WifiFw.c:931-1030` 不重排），PWRON 之后紧跟 `udelay(500)`（`1022-1028` vs `aic_bsp_driver.c:1507-1508`）。**唯一整体顺序差异**：厂商把整张 BT 表（含 PWRON）写在 `fmacfw` 上传与 `patch_config` **之前**（`aic_bsp_driver.c:1936-1945`），我们写在**之后**（`WifiFw.c:1651-1695`，apply 在 start 阶段）。地址分析表明两者不覆盖（§1.2），但 PWRON 的三条写是否会影响"刚上传完"的芯片，纸上定不了（实验 E6）。

---

## 2 · P0（不修不许写）

### P0-1 `WifiFw.c:1788-1814` · 独立 `patch apply` 缺"固件已驻留"前置

- **问题**：入口只检查 probe 成功、`c8a1:0082` 身份、通道可读（1790-1802），没有任何"adid/patch/fmacfw 本次上电已写入"的条件。而 `gAicPatchReady` 恒 0 使 `write confirm` 永拒（1841-1849），所以冷启动后芯片里**一定没有** fmacfw。
- **证据**：厂商的等价函数只被 `aicwifi_init` 在 `rwnx_plat_bin_fw_upload_android(RAM_FMAC_FW_ADDR, wl_fw)` 成功之后调用（`aic_bsp_driver.c:1717-1735`），从不单独跑 `patch_config`。表里 100 条的目标也全是 fw/adid/patch 镜像内部或配套 RAM。
- **后果**：(a) 100 条写落进"下次上传会被覆盖"的 RAM，日志却打 `applied ok`；(b) `gAicPatchApplied=1`（1311）随后让真正的 `write confirm` 在 start 阶段被 `patch already applied`（1266）打回，一次手滑废掉本次启动周期；(c) `str_base` 为野值时 magic 会写到任意对齐地址。
- **修法**：加 `gAicFwResident`，由 `AicFwPushStage` 成功写完 adid+patch+fmacfw 后置位，probe/上电清零；PATCH_APPLY 分支要求它为 1，否则 `patch apply needs fmacfw resident (run 'wififw write confirm')`。
- **证伪**：冷启动直接 `patch apply`，看打印的 `config_base/str_base` 是否落在 `[0x00100000,0x00210000)` 之外。

### P0-2 `WifiFw.c:1672-1680` · `write confirm` 路径短读 → 半张表照样 apply

- **问题**：这里只用 `AicFwReadChunk(...) != 0` 判读成功，**没有** `TableLen == Stat.Size`；而 `AicFwReadChunk` 只在 `Got==0` 时失败（1426-1430）。短读让 `Len` 偏小，`BuildPlan` 在 `938` 的 `Off + 24 <= Len` 处**静默截断**，半张表照样 `apply` → 置 applied → `start_app`。
- **证据**：表在 0x2F0 处是 PWRON、0x320 处是 TAF。若短读到 768 B（0x300），INF/TRAP/TB4/MODE 都在窗口内（0x290+24+72=0x2F0 ≤ 0x300），PWRON 与 TAF **整体消失** ⇒ 77 写，恰好不含 `0x40500128/0x4050012c` 的复位和 20 条 TAF。截断点落在记录边界上，不会触发 946 的 overrun 检查。
- **修法**：复用 `AicFwLoadPatchTable`（严格版，1336 已要求 `Got == Stat.Size`）的返回长度；或在 1676 后补一条长度相等判据；并在 BuildPlan 末尾要求 `Off == Len`。
- **证伪**：把表截到 768 B 放进 FAT，`dry` 应 FAIL（1336 严格），`write confirm` 却照旧往下走 —— 两者不一致即成立。

### P0-3 `WifiFw.c:946/938/806` · 长度字段溢出、尾部不严格、tag 只比 12 B

- **问题一**：`EntryLen` 直接来自文件，`Off + 24u + EntryLen * 8u` 在 32 位下**回绕**。`EntryLen ≥ 0x20000000` 时乘积为 0（或很小），检查通过；随后 `969` 的 `for (i=0;i<EntryLen;i++)` 以 8 B 步长读出 8 KB 静态缓冲区之外，得到"对齐的野地址"——`920` 的 `AicPatchAddrOk` 只挡 FMAC 区和非对齐，**挡不住**，于是真写。厂商同样没挡（`aic_bsp_driver.c:1328-1345`），但它是从整文件内核 buffer 读（会 oops），我们是不崩、直接写。
- **问题二**：循环条件 `Off + 24 <= Len`，尾部残留 1..23 B 被静默丢弃；厂商条件是 `p - rawdata < size`（1314）。
- **问题三**：注释写"按前缀匹配 16 B 更严"，实际 `806` 是 `i < 12`，比厂商的 13 B（含 NUL，`1311`）更松。
- **修法**：读到 `EntryLen` 立刻 `if (EntryLen > (Len - Off - 24u) / 8u) fail`（先除后乘，避免溢出）；循环改 `while (Off < Len)`，收尾 `Off != Len → fail`；tag 比 13 B；两处（表格解析 830 与计划构建 946）都要改。
- **证伪**：把表偏移 0x24 的 len 改成 `0x20000000`，`dry` 现码应打出 junk 地址清单或跑飞，修后应 `entry overruns`。

---

## 3 · P1

### P1-1 `WifiFw.c:873-889` · CFM 收了不看（可校验的 `memaddr` 被丢掉）

- **证据**：`struct dbg_mem_write_cfm { u32 memaddr; u32 memdata; }`（`aic_bsp_driver.h:271-274`）——芯片回显地址。厂商传 `NULL` 不校验（`aic_bsp_driver.c:398-411`），所以这不算"与厂商不一致"；但两条本地路径会因此假绿：① 某条超时后，迟到的 CFM 会被下一条请求按 id `0x403` 命中，之后所有"确认"整体错位一拍，而那次写其实是失败的；② `AicFifoWriteMsg` 块写失败会退回字节模式**重发**（391-403），重发成功 = 同一请求两条 CFM，同样错位。
- **附带**：`AicMemRead` 的 `Cfm[8]` 未初始化（631），`AicRecvMsg` 把 `ParamLen` 夹到 `Len-12`（612-614）；短 CFM 会 `return 0` 并让 `*Value` 取到栈残留 —— **这一个直接影响 `config_base/str_base/version`**。
- **修法**：写：`CfmLen >= 8 && Cfm[0..3] == Address`；读：`CfmLen == 8`；不匹配即判协议失步、失败并要求重新 probe。
- **证伪**：临时打印前 3 条写的 CFM `memaddr`，与请求地址比对（E3）。

### P1-2 `WifiFw.c:1280-1310` · 4 个 cfg 读排在 100 条写之后（晚失败）

- **问题**：最可能失败的步骤（`str_base` 读失败/非法）排在最不可能回滚的 100 条写之后。厂商这么排是因为它刚上传完 fw，读一定有效（`aic8800d80_compat.c:116-171`），我们没有那个前提。
- **修法**：Apply 里先做 cfg 的**纯读 preflight**（4 读 + 判据），通过后再写 100 条，最后写 cfg 的 10 条。这样 `str_base` 不合法时是 0 写退出。

### P1-3 `WifiFw.c:1192-1205/1227` · 地址合法性窗口太窄

- **问题**：只拒 `0` 和 `0xffffffff`。但这三个值的合法值域其实有界：fmacfw `0x120000..0x170E54`、patch `0x1E0000..`、adid `0x201940..0x201FEC`、pair 缓冲在 fw 镜像内的 `0x16F800`。
- **修法**：`StrBase`/`PatchAddr`/`ConfigBase` 三个都要求 4 对齐且落在 `[0x00010000,0x00210000)`；`Version > 0x06090100 && PatchBuff == 0` 直接失败（厂商会 `patch_addr=0`，`compat.c:162-171`，那更糟）。现在的 `1203` 是静默换回 `0x16F800`，属于"猜"。

### P1-4 `WifiFw.c:1266/1311/1684/1510` · `gAicPatchApplied` 的生命周期不对

- **问题**：(a) 它不是"本次上电"的——芯片掉电/重新 probe 后仍为 1；(b) `1684` 在调用方又置一次，出现双重真相（将来 Apply 内部语义一改，调用方仍会宣称已应用）；(c) 失败后没有"已污染"标记，允许立刻重跑——重跑不是续写，是从头重放，但芯片侧已吃过半个 PWRON 复位序列，且可能有一条超时请求的 CFM 还在队列里。
- **修法**：三态 `NONE/DONE/POISONED`；probe 成功清零；失败置 POISONED 并要求重新 probe；删掉 1684。

### P1-5 `WifiFw.c:988-1003/1058-1077` · MODE 数量与陈旧值

- **问题**：`gAicModeVal` 是 static，BuildPlan 只在 `i < 9` 时覆盖；表里 MODE 少于 9 对时自检读到的是上一次构建的残留（dry 之后 apply 会带着 dry 的值）。当前"少 9 对"会因 `baud=0` 被拒（运气），但没有显式判据。
- **修法**：BuildPlan 入口清零 `gAicModeVal`；BTMODE 记录必须**恰好 9 对且只出现一次**，否则失败。

### P1-6 `WifiFw.c:1307-1316` · 写完全程零证据

- **问题**：100 条写 + 10 条 cfg 写之后没有任何一次读回；`applied ok` 只证明"消息都收到了 CFM"。
- **修法**：读回 6 个哨兵，任一不等即失败：`0x40500150==1`、`0x1E7294==0x001E0000`、`0x20F600==0xF0223201`、`0x201FE0==0x00006F2F`、`StrBase+0x00==0x48435450`、`StrBase+0x0C==1`。

### P1-7 `WifiFw.c:61/1363-1368` · `AddrPatch==0` 仍走 `0x0020B43C` 兜底

- **问题**：兜底值与表里的 INF 值（`0x1E0000`）矛盾：真按兜底上传，INF 记录仍会把 `0x1E7294` 写成 `0x1E0000`，芯片就去一个没有 patch 的地址找 → 起不来而日志全绿。厂商 u02 的默认值也是 `0x20B43C`（`aic_bsp_driver.h:359`），所以这不是"厂商会成功"的路径。
- **修法**：D80 u02 下 `AddrPatch == 0`（或 `!= 0x001E0000`）一律硬失败；表是唯一真相。

---

## 4 · P2

- `1218-1225` 的 4 条 `block_size` 写在 pair 之前，厂商写在之后（`aic8800d80_compat.c:198-247`）。终态等价，但没必要保留差异。
- `1170` 的 `PairCnt = 1` 是硬编码：依赖 `Makefile:29-31`（`CONFIG_AMSDU_RX=n`、`CONFIG_IRQ_FALL=n`）与 `compat.c:58-65` 的 `USE_5G` 未定义。把这个出处写进注释，否则哪天开 AMSDU 就静默少一对。
- `805-806` 注释说"16 B"，代码比 12 B。
- `1150` 固定打 `rd=4`，第 4 读其实是条件读。
- `1300-1315` 进度数字自相矛盾：`Done` 最大 100、`Count` 101（含 delay），所以永远看不到 `100/101`，最后一行停在 `96/101`，随后又打 `applied: 101/101`。
- `1531` 打印 `bootstatus` 但不判（厂商用它填 `hwinfo_r`）。
- `AicPatchTableParse` 允许 INF 记录出现在任意位置，厂商只认 head（`aic_bsp_driver.c:1361-1369`）。目前无害。
- `920-928` 没有 per-type 白名单：PWRON 的 3 个地址、INF 的 2 个地址其实可以写死校验（真实表就这几个值）。

---

## 5 · 已经对的、别改坏了

1. **100 条 = 表字节，顺序 = 文件序**，不按类型重排（`931-1030`）。
2. **INF 写在 adid/patch 上传之后**。`0x1E7290` 就在 patch 镜像里，写早了会被上传覆盖；厂商也是先上传后 table_load（`1425-1430` → `1469`）。**这是整条链最容易被"整理顺序"改坏的一条。**
3. **PWRON 后的 `udelay(500)` 紧跟该记录**（`1022-1028`），与 `1507-1508` 一致。
4. **MODE 9 值映射正确**（§1.3），值来自 header 默认值而非表值，且 `Tbl` 保留了原值用于对账。
5. **cfg 的 4 读 + 10 写逐条对上**：读 `0x120198/0x1201A0/0x12001C/(0x1201A4 仅 ver>0x06090100)`；写 `str+0x00=48435450`、`str+0x08=50544348`、`str+0x04=pair_start`、`str+0x0C=pair_count`、`str+0x30..0x3C=0`、`pair+0=config_base+0x00B4`、`pair+4=0xF3010000`。
6. **先算后写**：`BuildPlan` 全量构建在 `1270`，任何一条地址不合法就 0 写（`1005-1010`）。
7. **不猜**：表不可用/信息缺失/`info_len==0` 一律失败（`800-834`、`1327-1339`），与 D80 语义一致（`1420-1422`）。
8. **失败不置 applied、二次调用拒绝、不做"续写"**（`1266`、`1297`）。
9. **不上传 `fw_patch_table.bin`**（它是主机侧数据）——正确。
10. **dry 完全不碰芯片**（`1242` 传 `Host=0`，`1326` `(void)Host`）。
11. **不预写 `0x40500150`**：D80 分支厂商也不预写（只有 DC 分支预写 `reset/adid_flag`，`1399-1409`），别照抄 DC。

---

## 6 · 真机证伪实验

| # | 构造 | 期望日志 | 能排除什么 |
|---|---|---|---|
| E1 | `wififw patch dry`（零风险，先做） | `0065 items = writes 0064 (mem=005b cfg=0009)`；`4/19/45/9/3/20`；`patch[00] 001e7290←00201940`；`patch[68..76]` 与本报告 §1.3 一致；`patch ver="- Mar 07 2024 14:29:05 - git f94a3e4"` | 卡上的表与 SDK 原件不一致（一破，全部结论重算）、计划漂移 |
| E2 | 把表尾部截到 768 B 放进 FAT，先 `dry` 再 `write confirm` | `dry` 必须 FAIL（`1336` 严格）；`write confirm` 若照旧往下走并在 start 阶段 apply，则 P0-2 成立 | 短读 → 半张表（P0-2 的判据） |
| E3 | 临时在 `AicMemWrite32` 打印前 3 条的 CFM `memaddr`（实验后撤） | 与请求地址相等 | 芯片是否回显 `memaddr`（决定 P1-1 能不能做）、当前是否已经错位一拍 |
| E4 | 冷启动直接 `wififw patch apply`，只看它打印的 `config_base/str_base/ver` 与退出顺序 | 修 P1-2 后应是"0 写 + str_base invalid"；修前是"先 100 条再报错" | 独立 apply 的驻留前提（P0-1）、cfg 读在未上传状态下的实际值 |
| E5 | 修 P1-6 后跑完整 `write confirm`，读回 6 个哨兵 | `0x40500150=1`、`0x1E7294=001e0000`、`0x20F600=f0223201`、`0x201FE0=00006f2f`、`str+0=48435450`、`str+0C=1` | "写了没落地"、"CFM 假绿"、地址算错 |
| E6 | apply（或 write confirm）完成后立刻 `wififw id` 再读 `0x40500000` | 仍返回 `c8a1:0082` 的 rev，消息通道仍活 | PWRON 的 `0x40500128/0x4050012c` 写在 fw 上传**之后**是否影响 bootrom（我们与厂商的顺序差异） |

---

## 7 · 证据分级与修改顺序

**真机验证过（只作对账，不作依据）**：dry 的 101 条清单与逐 type 计数、`patch[00]/[68..76]`；`c8a1:0082` 身份；一次成功的 `DBG_MEM_READ_CFM`。
**本次纸面推演（已用独立复算坐实）**：7 条记录的 offset/len；100 条地址/值；MODE 下标；cfg 10 写；地址体检（对齐、不落 FMAC）；顺序依赖。
**必须真机才能定**：CFM 是否回显 `memaddr`；`0x120198/0x1201A0/0x12001C` 在"未上传/已上传"两种状态下的实际值；PWRON 三条写对 bootrom 的副作用；芯片对非法地址 mem write 的反应（回 cfm 还是挂总线）。

**修改顺序（先做小且能被证伪的）**：
1. P0-3（约 10 行）→ 2. P0-2（3 行）→ 3. P1-2 + P1-3（约 30 行）→ 4. P1-1 + P1-6（约 20 行）→ 5. P1-4 + P0-1（约 15 行）→ 6. 最后才置 `gAicPatchReady = 1`（1 行），走 `write confirm` 全链路 + E5 哨兵。

**在这些之前，不要跑 `wififw patch apply`。**
