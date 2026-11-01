# Milk-V Duo S · AIC8800 WiFi SDIO 初始化与消息通道 — 独立评审

> 只读评审，**未改一行代码**（本文是唯一产物）。
> 评审对象快照（文件在 10:20–10:28 之间被改过一次，本评审以这份快照为准，行号以此为准）：

| 文件 | 行数 | 修改时间 | md5 前 8 |
|---|---|---|---|
| `Sdhci.h` | 163 | 10:27:31 | `d452947a` |
| `Sdhci/Sdio.c` | 1154 | 09:57:30 | `db47532a` |
| `Sdhci/WifiFw.c` | 1081 | 10:28:03 | `220efafc` |
| `Sdhci/Io.c` / `Command.c` / `Transfer.c` / `Utility.c` | 131 / 163 / 154 / 157 | 02:52 / 02:51 / 02:52 / 09:57 | `5160fdf3` / `1c071ac1` / `3368f27e` / `06ef1732` |
| `Shell/Commands/Wifi.c` | 272 | 10:28:40 | `b290d7da` |

> 对照物：`aic8800_fdrv/aicwf_sdio.c`（2591 行）、`aic8800_bsp/aicsdio.c`（2032 行）、`aic_bsp_main.c`（393 行）、`aic_bsp_driver.c`（57277 B）、`aic_bsp_driver.h`、`aic8800d80_compat.c`，以及 `device/generic/rootfs_overlay/duos/mnt/system/firmware/aic8800/` 里的**实物固件**（本评审对 `fw_patch_table_8800d80_u02.bin` 做了逐字节解读，见 §2-W3）。

---

## 0 · 结论先行

1. **「通道打通」目前只有一条真证据，却被三处「无条件绿」包着**：真证据是一次 `status=01 → fn1` 后读到的 **20 B `DBG_MEM_READ_CFM`**；而 `WiFi: sdio enum ok (cis)`、`Host->Ready=1`、`WiFi: fw state=done` 三者都不依赖任何一次成功的读取/启动（`Sdio.c:1141,1149-1152`、`WifiFw.c:979`），所以现在的绿不能证明目标达成。
2. **`wififw write confirm` 的 patch 落点与 SDK 自带 patch_table 冲突，且这张表的 46 条写一条都没做**：表里 `addr_patch = 0x001E0000`，代码写 `0x0020B43C`（§2-W3，有字节级证据），且 `0x0020B43C+30040` 正好盖住同表 TRAP 目标 `0x0020F6xx`——自相矛盾；表本身的 INF/TRAP/PWRON/PATCH_TAF 写全部缺失却仍 `start_app` 并打 `done`（§2-W4），这是**「看起来绿、芯片起不来」的确定来源**。
3. **有一处真·假绿在门控上**：IORx 读失败时函数拿上一轮 IOEx 的残留值判「fn1 enabled」（`Sdio.c:991-1012`）；只要 IOEx 的 bit1 已被前一次 probe/vendor-init 置上（真机 `ioex=02`），第二次起的 probe 即使 CMD52 全挂也会打 `fn1 enabled`。
4. **接收判据里 `masked>120 → fn2` 这一支从未在真机跑过**（唯一实测是 `status=01 → fn1`），而厂商 fdrv 对 D80 的**所有**分支都改成从 fn1 读（`aicwf_sdio.c:2086-2104` 把 `msg` 参数注释掉了）；这条待真机确认，不能当既有事实（§3-Y6）。
5. **诊断能力两极分化**：单条命令的现场（arg/r5 拆位/pst/int/blkcnt）已经足够定位「参数错」和「门控错」；但「格式错」（帧长/补齐/CRC/function 选错）没有任何本地自校验、也没有 TX 回显，只能表现成 timeout，和「硬件错」不可分（§3-Y16、§3-Y2）。

---

## 1 · 范围

### 1.1 这份代码是什么

- **是什么**：`Kernel/HAL/RiscV/Board/milk-v-duo-s/Sdhci*` 是 Duo S 上两颗 SDHCI 控制器的板级驱动。SD0 走 SD 卡（启动盘，本轮不动），SD1 走 SDIO，目标是把 AIC8800D80 从 ROM 枚举出来（CMD0/5/3/7 → CCCR/HS/4-bit → CIS/MANFID），再在 fn1/fn2 上跑 AIC 私有的 `DBG_*` 消息（mem read / mem block write / start app），最终从卡上的 `/Assets/Fw/...` 读固件下载。
- **入口**：开机由 `Sdhci.c:87/101` 无条件调 `SdhciWifiInitialize()`（SD0 成败都试 SD1）；人工入口是 shell 的 `wifiprobe` / `wififw`（`Wifi.c:248-254`）。日志出口在 `Utility.c:82-113`（有 sink 走 console，无 sink 直写 UART）。
- **不是什么**（本次不审）：SD/块设备的读写正确性（`Card.c`/`SdhciRead/Write`）、SDHCI 电气时序与 PHY 调参是否精确、CMD52/53 在 4-bit/HS 下的眼图余量、WiFi MAC/协议栈（`HalWifi*`，目前是桩）、BT 通道、以及中断（DAT1）路线——本轮全部是轮询。

### 1.2 四个核心问题的定位

| 问题 | 直接答案落在 |
|---|---|
| Q1 验收判据对不对 | §2-W2、§2-W6、§2-W10、§3-Y2；§6.2 给了可执行的硬门禁 |
| Q2 失败会不会假绿 | §2-W1、§2-W4、§2-W5、§2-W7、§3-Y5、§3-Y9 |
| Q3 有没有副作用 | §2-W7、§2-W9、§3-Y13、§4-I9 |
| Q4 失败信息够不够定位 | §2-W10、§3-Y1、§3-Y2、§3-Y4、§3-Y12；§6.3 给证伪清单 |

### 1.3 四个问题的直答

**① 验收判据对不对？** 不对，至少三处「无条件绿」：`Sdio.c:1141` 的 `enum ok (cis)` 与 CIS 解析结果无关、`Sdio.c:1149-1152` 的 `Ready=1` 与身份无关、`WifiFw.c:979` 的 `fw state=done` 与芯片是否启动无关。整条链上真正能证明「消息通道通」的只有一条证据：`status=01 → fn1 → 20 B CFM（hdr 里 type=0x11、id=0x0401、memdata 合理）`。换句话说：**今天的绿 ≈「代码跑到了行尾」，不是「目标达成」**。

**② 失败会不会假绿？** 会，两处硬的：门控（`Sdio.c:991-1012`，IOEx 已有 bit1 时 IORx 读失败仍打 `fn1 enabled`）与 CFM 解析（`WifiFw.c:608-627`，不校验 `memaddr`/`CfmLen`，栈残留也能当 `memdata`）。再加一层 shell 把返回码丢掉（`Wifi.c:249`），失败在操作者眼里只剩「少了一行日志」。

**③ 有没有副作用？** 默认枚举路径已经干净（FIFO 自检被挪出，`Sdio.c:1135-1142`），但两个「只读」入口不干净：`wififw fifotest` 往真发送 FIFO 推 1536 B 并盲读接收 FIFO（`Sdio.c:459,470`）；`wififw write`（不带 confirm）会写 5 个配置寄存器并向芯片发一条消息（`WifiFw.c:227-267`），日志却写 `NOTHING WRITTEN`。

**④ 失败信息够不够定位？** 分四类看：**参数错**够（`arg=`、R5 拆位、`blksz/blkcnt/tmode`）；**门控错**够（`hctl/pwr/clk/inhibit`）；**硬件错**勉强（`pst`/`int`/错误位都在，但 100 ms 轮询会把「慢」报成「无响应」，§Y11）；**格式错**不够——发送侧没有帧头回显（§Y16），接收侧不校验短帧（§Y3），而 `Io.c:94-96` 的静默 `-1` 让「参数错」和「硬件错」同码（§Y1）。

---

## 2 · ⚠️ 会改外显行为

### W1 `Sdio.c:991-1012` · IORx 读失败被当成「fn1 已开」（Q2）

- **现状**：`991` 把 IOEx 读进 `Data`，`992` 写 `Data|0x02`；`998-1005` 每毫秒读 IORx 到**同一个 `Data`**，`999` 读失败就 `break`，`1007` 仍用 `Data & 0x02` 判定；只有成功才 `SdioFail`。
- **为何错**：`999` 的 `break` 把「CMD52 读失败」和「读到但还没置位」混在一起，`1007` 再用 `Data` 判定。而 `Data` 在失败时是 `991` 读到的 **IOEx** 值；只要本次 probe 之前 IOEx 的 bit1 已经是 1（重跑 `wifiprobe`、或先跑过 `wififw` 的 vendor-init，真机 `NOTES.md:434` 记的就是 `ioex=02`），`Data & 0x02` 恒真 → 打 `fn1 enabled`。**首次 probe（IOEx=0）不会假绿**，所以这条只在「第二次及以后」出现——也正因如此它很难被一次干净的真机日志抓到。
- **建议**：进循环前 `Data = 0`；读失败单独 `SdioFail("IORx read")`；判据改成「读到过且本轮 `&0x02`」。`waited=` 现在打的是循环次数（`1009`），改成真实毫秒（每次迭代还含一次 CMD52 的耗时）。

### W2 `Sdio.c:1141` + `1149-1152` · `enum ok (cis)` 与 `Ready=1` 无条件成立（Q1）

- **现状**：CIS 解析失败的兜底路径（`1065-1107`）会打 `no MANFID tuple, continue without module id`，然后**照样**打 `sdio enum ok (cis)`、`Ready=1`、`gWifiSdioState=1`、`probe end ok`；`SdioChipIdentify()` 的返回值在 `1115` 被 `(void)` 丢掉。
- **为何错**：验收行里写着 `(cis)`，但它跟 CIS 结果无关。真机上只要枚举走到末尾，无论 `manfid` 是 `c8a1:0082` 还是 `0000:0000`，shell 都会打 `wifi: SDIO probe ok`（`Wifi.c:152`）。这直接违反「验收判据要能证明目标达成」。
- **建议**：`WiFi: sdio enum ok (cis)` 只在 `CisOk==0 && Cis.SawManfid!=0` 时打；否则打 `WiFi: sdio enum ok (no cis)`，并且 `Ready` 只表示「总线可用」，另设 `gWifiIdentity` 表示「身份确认」，shell 的成功行按 `Ready && Identity` 给。`SdioChipIdentify` 的返回值要参与判定（未知芯片 → 至少 `Identity=0`）。

### W3 `WifiFw.c:58` + `694-699`（patch 行 `696`） · patch 落点 `0x0020B43C` 与 SDK 表的 `0x001E0000` 冲突（Q4 预备）

- **现状**：`AIC_D80_U02_PATCH_ADDR 0x0020B43Cu`，阶段表 `{"patch", AIC_FW_PATCH, AIC_D80_U02_PATCH_ADDR, 30040u, 0}`，注释引 `aic_bsp_driver.h:358-359`。
- **为何错**：`aic_bsp_driver.h:358-359` 只是**兜底值**。厂商对 D80 在 `aic_bsp_driver.c:1417` 调 `aicbt_patch_info_unpack()`，**用表里的值覆盖** `addr_adid/addr_patch`，随后 `:1425/:1427` 才上传；且 `:1418-1421` 对 D80 强制 `info_len!=0`，即这张表是必需件。表的实物证据（`xxd -s 0x28 -l 32 fw_patch_table_8800d80_u02.bin`）：
  ```
  0x28: 90 72 1e 00   adid_addrinf  = 0x001E7290
  0x2c: 40 19 20 00   addr_adid     = 0x00201940   ← 与代码一致
  0x30: 94 72 1e 00   patch_addrinf = 0x001E7294
  0x34: 00 00 1e 00   addr_patch    = 0x001E0000   ← 与代码 0x0020B43C 不一致
  ```
  独立旁证：同表 TRAP 段的写目标是 `0x0020F600/0x0020F604/...`（文件偏移 `0x60` 起），而 `0x0020B43C + 30040 = 0x00212994` **正好覆盖**这段地址——两个数不可能同时成立。
- **建议**：把 patch 落点改成从表里读（`addr_adid/addr_patch`），硬编码值只做「表不可用」时的兜底并打日志区分。落点校验一条就够：下载后读 `0x001E0000` 应等于 `fw_patch.bin` 首字 `0x681B4B02`。
- **证伪**（见 §6.3-F2）：若我错了，应该能看到 `0x0020B43C` 处读出 patch 首字、而厂商驱动日志里 `aicbt_patch_info_unpack` 后 `addr_patch` 仍打印 `0x0020b43c`——源码里没有这条打印，也没有二次覆盖。

### W4 `WifiFw.c:672-684` + `933-982` · patch_table 的 46 条写没做，却 `start_app` 并打 `done`（Q1/Q2 预备）

- **现状**：注释已诚实写明「那一步本版**未实现**」，但 `WifiFwWrite()` 仍然跑完 `adid → patch → fmacfw → start_app`，最后打 `WiFi: fw state=done (all stages ok)`。
- **为何错**：厂商顺序是 `adid → patch → patch_table(逐条写 RAM) → fmacfw → patch_config_8800d80 → sys_config → start_app`（`aic_bsp_driver.c:1738-1795`）。我逐字节解了这张表，`aicbt_patch_table_load()`（`:1469-1512`）会做 **46 条非 BT 写**：INF 4 条（含 `0x40500150←1` adid 标志）、TRAP 19 条（`0x0020F6xx` 跳板 + `0x406900xx` 控制块，交替）、PWRON 3 条（`0x40500128/0x4050004C/0x4050012C`，之后 `udelay(500)`）、PATCH_TAF 20 条；另有 BTMODE 9 条（BT 参数）。缺这些写，fmacfw 即使下载完整也不会正常工作。而 `aicwifi_patch_config_8800d80()`（`aic8800d80_compat.c:114-245`）还要先读**芯片里的 4 个字**（`0x120198` → `config_base`、`0x1201A0` → `aic_patch_str_base`、`0x1201A4` → `patch_buff_base`、`0x12001C` → 版本），再按版本决定表落点、把 `patch_tbl_8800d80[]` 逐项写进去，最后写 `AIC_PATCH_ADDR(magic_num/pair_start/pair_count/block_size[])`——这一步也没做。注意这 4 个字**必须读**：本镜像静态内容里 `0x198/0x1A4` 就是 0（`xxd -s 0x190 -l 32`），显然由芯片在运行时填，猜不得。
- **建议**：`done` 只能在「表写 + patch_config」都实现并通过后打。在此之前，`wififw write confirm` 的输出里必须有一条显式的 `WiFi: fw boot-critical steps missing (patch_table, patch_config) — expect no boot`，且不要把 `start_app` 当成功路径（见 W5）。

### W5 `WifiFw.c:870-890` + `972-980` · 启动后没消息也算 done（Q1/Q2）

- **现状**：`AicFwWaitBootMsg()` 返回 `void`，`875` 只打 `boot poll: no msg in 2000ms`，`979` 紧接着打 `fw state=done`。
- **为何错**：「固件下载成功」的唯一强判据是**芯片跑起来并回话**（厂商 `aicwifi_start_from_bootrom()` 还要再等 `aicwf_hostif_ready`）。现在这条判据缺失，`done` 等于「我发完了」，不等于「它活了」。
- **建议**：`AicFwWaitBootMsg` 返回 int；两秒内没有 `0x11/0x12` 帧就判失败并打 `fw state=failed (no boot msg)`。注意不能拿 `start_app_cfm.bootstatus==0` 当判据——厂商把 `bootstatus & 0xFF` 当 `hwinfo` 用（`aic_bsp_driver.c:1559`），它本来就可能非 0。

### W6 `WifiFw.c:608-627` · 「chip rev」读操作不可证伪（Q1）

- **现状**：`AicMemRead()` 把 CFM 参数前 8 B 拷进 `UINT8 Cfm[8]`（**未初始化**，`610`），只用 `Cfm[4..7]` 当 `memdata`，不看 `CfmLen`、不看 `Cfm[0..3]`（`memaddr`）。`WifiFwChipRev()`（`1004-1026`）只判「读没读成功」，值是什么都算通过。
- **为何错**：`dbg_mem_read_cfm` 是 `{memaddr, memdata}` 两个字段（`aic_bsp_driver.h:282-285`）。不校验 `memaddr` 就无法区分「真读到」和「拿到别的 CFM / 参数长度不足 8 时读栈残留」。而真机记录的 `raw=40500000` 恰好**等于请求地址**，按厂商判定 `chip_rev = memdata>>16 = 0x50` 又不在 `CHIP_REV_U01/U02/U03`（`1/3/7`，`aic_bsp_driver.h:478-481`）里——这两点合起来必须排除「把 memaddr 当成了 memdata」。
- **建议**：① `UINT8 Cfm[8] = {0}`；② `CfmLen != 8` 直接失败；③ 打 `cfm.memaddr` 并与请求地址比对；④ 再加一条**对照地址**（例如读 `0x40500004`）要求两次值不同，才算「通道通」。这一步是 `wififw id` 升格成判据的最小代价。

### W7 `Sdio.c:666-690` + `439-481` · `fifotest` 污染真通道、恒返回 0、读 4 B 会截断消息（Q3/Q2）

- **现状**：`SdhciWifiFifoTest()` 无论 `SdioFifoTest` 干了什么，`690` 都 `return 0`；`SdioFifoTest` 往 **fn1 0x10（真发送 FIFO）** 推 1536 B 无帧头的图案（`459`），再往 **fn1 0x0F（真接收 FIFO）** 盲读 4 B（`470`）。
- **为何错**：1536 B 不是回环缓冲，是消息通道；芯片会把它当一条畸形消息吃进去。更实际的风险是 `470`：若此时恰有一条 512 B 的 CFM 待收，先读 4 B 会把消息头吃掉，后续 framing 全错位（`AicRecvMsg` 只按 `hdr[2]`/`id` 过滤，错位后会一直 skip 到超时）。而「恒返回 0」让这个诊断动作永远不会红。
- **建议**：`fifotest` 只保留「写方向」并**删掉 `470` 的盲读**（要探读就按 fn1 `0x04` 的真实块数读，且读完整消息）；函数返回真实结果；命令结束时强制 `DuoWifiPowerOn()` 断电重来（或在输出里写死「跑完请 `wifiprobe`」），否则下一次 `wififw` 的基线不可信。

### W8 `WifiFw.c:68-72` · 固件目录不存在，且与仓库既有约定不一致（Q1 阻塞）

- **现状**：`/Assets/Fw/aic8800/` 下的 `fw_adid.bin / fw_patch.bin / fw_patch_table.bin / fmacfw.bin`。
- **为何错**：仓库里**没有任何 `Assets/Fw`**：`Runtime/RootFs/RiscV/Assets` 只有 `Fonts/Icons/Images/Locale/Packs`（连 `Firmware/` 子树都没有）；x86 客体的固件约定是 **`Assets/Firmware/<vendor>/`**（`Runtime/Assets/Firmware/brcm/*.bin`，由 `Kernel/build.sh:225-229` 种进镜像）。也就是说 `/Assets/Fw/aic8800/*` 这条路今天在任何一套根文件系统里都不存在，`read` 与 `write confirm` 的第一行必然是 `fw read: missing …`——`wififw` 全链路**不可能有绿**，`NOTES.md:267-271` 记的那条判据行也就永远不会出现。
- **建议**：二选一，但必须落到「文件真的在盘上」：① 跟随既有约定改 `Assets/Firmware/aic8800/`，并在 RiscV 根文件系统种子里建同名子树；② 保留 `/Assets/Fw/aic8800/`，则把「拷 4 个文件」写进 Duo S 制盘步骤并写进 NOTES。真机快速通道：厂商卡上已有 `/mnt/system/firmware/aic8800/{fw_adid,fw_patch,fw_patch_table,fmacfw}_8800d80_u02.bin`，直接 `cp` 成短名即可先跑通。另外 `read` 应加「预期长度」表（1708 / 30040 / 1048 / 331348），长度不符直接失败——同时防「换错芯片版本」。

### W9 `WifiFw.c:227-267` + `1014/1057` · 「检查模式一个字节都不写」是假的（Q3）

- **现状**：`wififw write`（不带 confirm）走 `WifiFwCheck()`，日志在 `928` 打 `NOTHING WRITTEN`；但前置的 `WifiFwChipRev()`（`1057`）内部先 `AicWifiVendorPrepare()`（`227-267`）：写 IOEx、写 FBR `0x110/0x111`、写 fn0 `0xF2=0x7F`、写 fn1 `0x07=0x01`，再**发一条 `DBG_MEM_READ_REQ` 进发送 FIFO**。
- **为何错**：措辞会误导操作者（真写前的那次「只读」已经把芯片配置改了一遍；若上一轮下载中断，这次写入还会落在半初始化状态上）。它同时也是 `id` 命令的副作用——`Wifi.c:201` 的注释写「只读」。
- **建议**：检查模式只做「文件 stat + 用枚举缓存里的 `gWifiChipVendor/Device` 判身份」，不碰芯片；或者把日志改成 `check done (chip touched: vendor-init + 1 mem-read, no fw written)`。`AicWifiVendorPrepare` 里除 IOEx 外都无条件写（`250-263`），建议一律改成「读回不等才写」，让幂等性可观测。

### W10 `WifiFw.c:985-1001` · `wififw read` 名不副实：只 stat，不读（Q1）

- **现状**：注释还写着「S5a 的读层自证，仍保留」，但代码只有 `FileSystemFileStat` + 打字节数（`988-999`），没有一次 `FileSystemReadFileAt`。
- **为何错**：`read` 的判据行（`fw read … ok`）现在不证明读路径可用；文件读失败（坏簇/短读）只能在 `write` 阶段才暴露，而那时已经在写芯片了。
- **建议**：`read` 至少读首尾各 512 B 并比对长度；把「预期长度」也放进来（见 W8）。

---

## 3 · 🟡 隐患

### Y1 `WifiFw.c:370-381` + `Io.c:94-96` · 块→字节回退对 >512 B 永远不可能成功，且参数错与硬件错同码（Q4）

- **现状**：块写失败后用 `SdhciIoRwExtended(..., Blocks=0, BlockSize=(UINT16)Padded, ...)` 重试；而 `SdhciIoRwExtended` 在 `BlockSize > 512` 时**不发命令直接 return -1**（`Io.c:94-96`）。真实消息补齐后最小 512、1 KB 块写是 1536，所以这条回退只会打出 `tx byte write FAIL`，一次总线操作都没做。
- **建议**：回退条件加 `Padded <= 512`；`SdhciIoRwExtended` 增加「本地拒绝」的错误码（或先打一行 `arg reject`），别让「参数错」冒充「硬件错」。这一条对 Q4 的四分法很关键。

### Y2 `WifiFw.c` 29 处 `\\n` + `Wifi.c:249` 丢弃返回值 · 日志边界不可靠（Q1）

- **现状**：新写的固件段（`866,876,881,886,900,910,917,921,926,928,950,962,980,989,993,995,999,1009,…`）里是 `\\n`（源码两个反斜杠，C 里等于字面 `\n` 两个字符），例如 `SdhciMark("WiFi: fw state=done (all stages ok)\\n")`。同文件其它行（`713`）是正常的 `"\n"`。
- **为何错**：这些正是「判据行」，不换行会和下一条日志粘在一起；按行 grep 的验收脚本会漏。另外 `Wifi.c:249` 把 `SdhciWifiFw` 的返回值丢弃，`wififw` 没有 `ok/FAILED` 总结行（对比 `wifiprobe` 有，`Wifi.c:152`）。
- **建议**：全量替换为 `"\n"`；`wififw` 末尾按返回值打一行 `wififw: ok` / `wififw: FAILED`。

### Y3 `WifiFw.c:558-562` + `575-601` · RX 帧长与参数都不做下界校验（Q2/Q4）

- **现状**：`AicRecvMsg` 在读之前只判 `Len>512` 就截断（`558-562`），不看 `Len>=12` 就取 `gAicRx[2]/[4]/[10]`；`ParamLen` 只做上界钳制。
- **为何错**：`Len==4` 的畸形帧会用上一次的残留字节去匹配 `type/id`，可能命中一条「假 CFM」；`Len>512` 时只读首块，卡侧 FIFO 里剩下的半条消息会在下一次轮询被当成新消息头，framing 从此错位（`wififw write` 每次 1 KB 块写都会踩到这条路径的机会）。
- **建议**：`Len < 12` 直接判格式错并打 `rx short frame len=`；`Len>512` 时把剩余部分读空（或直接 RESET 该 function 的 FIFO 并重同步），别留给下一轮。

### Y4 `WifiFw.c:459-464` · 轮询没有「连续错误」上限（Q4）

- **现状**：`SdhciIoRead8` 失败只 `delay(1ms); Waited++`，`Waited` 只统计睡眠，不统计命令耗时；`SdhciCommandSend` 单次最多等 100 ms（`Command.c:113,123-136`）。
- **为何错**：总线已死时，500 次轮询 × 每次最多 100 ms ≈ 50 s，串口在几十秒里一行不出，排障时会被误判成「卡死」。真正的超时也无法解释成「500 ms」。
- **建议**：连续失败 N 次（比如 5）就提前返回 `bus dead`；`Waited` 用 `SdhciReadTime()` 算真实毫秒。

### Y5 `WifiFw.c:502-504` · `len==0` 一律按 512 读（Q2）

- **现状**：`if (*OutLen == 0u) *OutLen = 512u;`。
- **为何错**：`status=0x80`（`masked=0x88>120`，非 127）时按公式 `(status&7)*512 = 0`，厂商这一支实际什么都不做（`aicwf_sdio.c:2094-2097` 用 `data_len=0` 读，`recv_pkt` 直接 `-EINVAL`）。我们改成 512 就会去读一个空 fn2，必然超时并触发总线复位——把「本来无事」变成「复位」。
- **建议**：`len==0` 就 `continue` 轮询（并打一行 `len=0 skip`），不要发明长度。

### Y6 `WifiFw.c:470-494` · `>120 → fn2` 这一支是纸面实现，厂商 fdrv 反而一律读 fn1（Q1，待真机确认）

- **现状**：`Masked>120` 时 `*OutFn=2`，`AicFifoRead` 用 `Fn=2` 读 `0x0F`；真机唯一实测是 `status=01 → fn1`（`NOTES.md:341-355`）。
- **为何错**：`aicwf_sdio.c` 的 D80 分支里，厂商把 `aicwf_sdio_readframes(sdiodev, 1)` 的 `msg` 参数**注释掉**，改成无参版本——而无参版本恒用 `sdiodev->func`（fn1）（`aicwf_sdio.c:2087-2104`、`:1170-1205`、`:199-215`）；BSP 版的 fn2 路径依赖 `func_msg`，而 `func_msg` 只在 DC/DW 被赋值（`aicsdio.c:316-318`），对 D80 是 NULL。也就是说：**厂商两条代码路径对 D80 都不读 fn2**。我们这一支若真被触发，可能拿到 `FUNCTION_NUMBER` 或干脆超时。
- **建议**：先在真机上把这一支跑出来（构造连续两帧、或对 fn2 直接做一次 CMD53 读）；在此之前 `>120` 也走 fn1，并把 `fn=02` 降级成 `待验证`。这条不阻塞当前功能，但会决定 S5 以后所有异步消息的读法。

### Y7 `Sdio.c:402-409` · 降速对照「卡侧仍 4-bit、主机 1-bit」不成立（诊断可信度）

- **现状**：注释称「卡侧仍是 4-bit，CMD53 只走 DAT0 时无影响」，于是只写 `HOST_CONTROL=0` 就在 25 MHz/1-bit 下做 CMD53 读。
- **为何错**：总线宽度是**双方协商态**（CCCR `0x07`），主机切 1-bit 而卡仍在 4-bit 时，卡的 DAT1-3 仍被当数据线驱动，这是规范外的组合。用它来「对照」50 MHz/4-bit 的结果，结论不可靠（可能把「宽度不匹配」误判成「速率问题」）。
- **建议**：对照前先把卡也写回 1-bit（`CCCR 0x07=0`），对照完再恢复；或者干脆砍掉这条对照，直接用 `SdhciWifiFifoTest` 的消息级验证替代。

### Y8 `Sdio.c:330,347-358` · CMD52 读失败时拿未初始化的 `Ref[]` 比对（Q4）

- **现状**：`Ref[16]` 未初始化；`347` 读失败就 `break`，但 `354-358` 仍对全部 `Bytes` 比较 `Buf[i] != Ref[i]`。
- **为何错**：读失败的正确结论是「无法判定」，现在会随机报 `mismatch +k`（k 落在已读区间之后时尤其误导），把「CMD52 挂了」伪装成「DAT 读错」。
- **建议**：读失败立刻单独返回并打 `cmd52 ref read fail @i`；比较只跑已成功读到的前缀。

### Y9 `Sdio.c:1036,1058,1065,1076` · `Cis` 未初始化就参与判定（Q2）

- **现状**：`SDIO_CIS_INFO Cis;` 在 `1036` 声明；`1058` 只在 `CommonPtr!=0` 时解析；`1065/1076` 却直接读 `Cis.SawManfid`。
- **为何错**：Common CIS 指针读失败（`1044-1047` 把 `CommonPtr` 置 0）时，`Cis` 是栈上残留；`SawManfid` 若碰巧非 0，会跳过两条兜底扫描并继续往下走，行为不可复现。
- **建议**：声明处 `= {0}`，并把「`CommonPtr==0`」显式记成 `CisOk=-1`（现在靠 `SawManfid` 兜，逻辑上是对的，但依赖未初始化内存）。

### Y10 `Sdio.c:647-656` · FORCE 重跑不清芯片身份（Q2）

- **现状**：`SdhciWifiProbe(FORCE)` 清了 `CardType/BusWidth/HighCapacity/RCA/Ready`，但 `gWifiChipVendor/Device`、`gWifiHostControl/gWifiRestoreHertz` 保持上一轮的值。
- **为何错**：一次失败的重跑之后，`SdhciWifiChipIsD80()` 仍可能返回 1（旧的 `c8a1:0082`），而 `Ready=0` 是唯一拦住 `wififw` 的东西——闸只剩一道。
- **建议**：FORCE 时把 4 个静态量一起清零；`SdhciWifiChipIsD80` 里再加 `SdhciWifiReady()` 前置条件。

### Y11 `Command.c:123-136` · 100 ms 软件轮询可能比控制器超时短（Q4，硬件/门控误判）

- **现状**：命令发出后最多轮询 1000×100 µs；没有等 `CMD_TIMEOUT` 错误位。
- **为何错**：SDHCI 自己的超时由 `TIMEOUT_CONTROL=0x0E` 决定（最长数秒）。若卡在 CMD5/CMD52 上反应慢于 100 ms，我们会先撤，然后 `RESET_CMD|RESET_DATA`，日志里 `LastStatus` 是「无响应、无错误位」——和「门控没开」长得一模一样。`NOTES.md:107-135` 那轮 CMD5 排查里，这类假象最容易带偏。
- **建议**：优先判 `SDHCI_INT_ERROR`（含 `CMD_TIMEOUT`），软件轮询至少放到 1 s；或在 100 ms 后先打一行 `cmd N no resp in 100ms, status=…` 再继续等。

### Y12 `Io.c:31` + `Sdio.c:269,277-280` · `Host->LastStatus` 被 R5 复用，现场会串味（Q4）

- **现状**：`SdhciR5Check` 失败时把 **R5 值**写进 `Host->LastStatus`（`Io.c:29-32`）；而 `SdioMarkXfer` 把 `LastStatus>>16` 当 INT_STATUS 的错误位解释并逐位命名（`Sdio.c:269,230-260`）。
- **为何错**：CMD52/53 失败后调用 `SdioMarkXfer`，`err=` 与 `bits:` 打印的是 R5 的高 16 位（含 R5 标志与 data），可能凭空出现 `DAT_CRC`/`CMD_TIMEOUT` 之类不存在的错误位。
- **建议**：加独立字段 `LastR5`（或 `LastErrorKind`），`LastStatus` 只放 INT_STATUS；`SdioMarkXfer` 按「最近一次失败类型」选择解释器。

### Y13 `Transfer.c:38,91,116` · 每 512 B 等 500 ms，缺少整体预算（Q4）

- **现状**：`SdhciPioRead/Write` 每块等 500 ms（写结束等 2000 ms），`SdhciCommandSend` 再等 100 ms，`SdioRecoverBus` 还有 RESET_ALL 分支。
- **为何错**：一次「卡没在发数据」的 1536 B 读最坏会耗 3×500 ms（还叠加重试），`wififw` 一轮 324 条消息时若总线半死，耗时不可预期；串口上只有一行 `PioAbort`，看不出是第几块失败。
- **建议**：给每个数据相一个总预算（例如 `Bytes/512 * 50ms` 封顶 2 s），并在 abort 行里带 `off=` 与 `blkcnt=`。

### Y14 `WifiFw.c:696` · patch 被标成 optional（与厂商必需行为不符）

- **现状**：阶段表 `{"patch", …, 30040u, 0}`，`Required=0` 缺文件就 `skipped (optional, no file)` 继续。
- **为何错**：厂商对 `bt_patch` 是**无条件**上传并在失败时 `return -1`（`aic_bsp_driver.c:1427-1428`）；u02 的 patch 里含 `0x1e7290/0x1e7294` 两个头槽要回填（见 W3/W4），缺它必不启动。
- **建议**：改 `Required=1`；真要允许缺失，就必须同时关掉 `start_app`（否则等于「缺件也点火」）。

### Y15 `WifiFw.c:1069-1076` · 失败后没有恢复指引（Q4）

- **现状**：失败只打 `fw failed at stage N, sent=…` 和 `fw stage end (failed)`。
- **为何错**：`start_app` 一旦发出，芯片就跳进 RAM 里的镜像；半成品镜像只会挂死。此时唯一的恢复手段是**给 GPIOA15 断电脉冲**（`Power.c:87-93`，即 `wifiprobe` 会做的那次），但没有任何一行日志告诉操作者。
- **建议**：任何 `fw failed` 之后固定追加一行 `WiFi: chip left in ROM state; run 'wifiprobe' to power-cycle before retry`；并在 `WifiFwWrite` 入口加一道「上次失败未恢复则拒绝」的软锁。
- **补一句「厂商怎么避免半途不可恢复」**：`aicwifi_start_from_bootrom()` 之前的所有动作都只写 RAM（adid/patch/fmacfw/表项），失败只要重新枚举 + 重传；唯一不可逆的是 `start_app`，厂商把它放在最后，且之后还要等 `aicwf_hostif_ready` 才算完成。**我们的阶段顺序照抄对了（start 是最后一阶段），所以「失败不半途而废」这件事在结构上是成立的**；缺的只是「失败后如何回到可重试状态」的说明与硬锁——这两条补上，就不需要额外的状态机。

### Y16 `WifiFw.c:352-362` · 发送侧没有帧头回显，「格式错」只能靠超时发现（Q4）

- **现状**：`AicFifoWriteMsg` 发之前只打 `fc= / padded= / blocks=`，不打这条帧的 `hdr[0..3]`、`id`、`param_len`、CRC；也不做「12 位长度字段 == 总长-4」的本地自校验（靠构造保证）。
- **为何错**：一次超时里，操作者无法区分「framing 写错（len/crc/type）」和「芯片没处理」。而 framing 恰恰是最容易抄错、又最贵的一类错误（手册里 0x11/0x12、len 含不含 4 B、crc 覆盖几位）。
- **建议**：在 `AicFifoWriteMsg` 里固定打一行 `tx hdr=<8 hex> id=<id> plen=<n>`，并在 `AicSendMsg` 里断言 `(hdr & 0xFFF) == Body+4`。成本一行日志，收益是「格式错」第一次出现就能被逐字节比对。

---

## 4 · ℹ️ 说明

### I1 `WifiFw.c:653` — 用循环变量当输出参数
`AicRecvMsg(Host, …, &i)` 把 `i`（`for (i=0;i<Step;i++)` 的计数器）当 `OutParamLen` 输出；功能无害（下一轮会重置），但可读性差且容易被误改。建议局部 `UINT32 CfmLen;`。

### I2 `WifiFw.c:702,979` — `gWifiFwState` 写而不读
`gWifiFwState = 2` 之后再无任何读者，是死状态。要么接进 §W5 的门（`done` 只在 `state==2 && boot` 时打），要么删掉。

### I3 `Sdhci.h:146-151` — 两套位域共用 1u
`SDHCI_WIFI_PROBE_FORCE 1u` 与 `SDHCI_WIFI_FW_READ 1u` 不同域却同值，`Wifi.c` 里还硬编码 `1u/2u/4u/8u`（`134,142,209,216,223,225`）。建议 FW 位改成 `1u<<8` 起，或干脆分开两组宏。

### I4 `Wifi.c:17` — 不存在的弱符号
`extern UINT32 SdhciWifiProbeForceFlag(void) __attribute__((weak));` 全仓库没有定义，永远是 NULL 且无人调用。删。

### I5 `WifiFw.c:62` — 字节模式注释与厂商注释相反
代码写 `fn1 0x07=0x01` 并注「0x01 = 允许字节模式」；厂商同一处写 `u8 byte_mode_disable = 0x1;//1: no byte mode`（`aicsdio.c:1727,1822,1893` 三处同名注释/写入）。真机既能字节模式读 CFM，说明这颗片子上 `0x01` 不拦字节模式——**按「实测优先」保留即可**，但别把注释当契约：将来若某版固件拒绝字节模式，第一嫌疑就是这一位。建议注释改成「厂商注释称 1=禁用，实测 1 仍可字节模式，保留并留观测点」。

### I6 `Sdio.c:392` — 用 CMD53 读 fn0 的 CIS
SDIO 简化规范里 function 0 只保证 CMD52；CMD53 打 fn0 属于未定义用法（许多卡会拒）。这张卡接受了，但**不要把它当「DAT 读方向」的唯一证据**（§W7 建议里已把它降级）。

### I7 `WifiFw.c:810-823` — 校验是 1 字 / 8 KB
`AicFwPushStage` 每 8 KB 只回读首个字（331 KB 的 fmacfw 只查 41 个字）。它能抓「地址错/长度错/整块没写」，抓不到「中间某块内容错」。建议在 `verify` 阶段再抽样 1 块（1 KB 全比），成本可接受。

### I8 `Power.c:227-235` — 卡检测极性自测会留下 `sw=0`
反极性分支成功时直接把 `TOP 0x294` 留在 `sw=0`（只有两种极性都失败才恢复 `Before`）。这是共享寄存器，若以后 U-Boot/其它驱动也看这一位，会互相打架。至少在这一分支打一行「kept sw=0（原值 0x…）」。

### I9 `Power.c:87-91` — GPIOA 的读-改-写
`*Ddr = *Ddr | BIT` / `*Dr = *Dr & ~BIT` 是整寄存器 RMW；若同端口其它脚由别的驱动异步改，可能被覆盖。当前单线程、独占，可接受，但值得在注释里写明前提。

---

## 5 · 写对的部分（避免下一刀误伤）

1. **帧头算术与厂商逐字一致**：`len = 总长-4`、type 固定 `0x11`、crc8 只算前 3 字节、4 B dummy（`WifiFw.c:398-403` ↔ `aic_bsp_driver.c:258-283`）。crc8 多项式/初值与 `aicsdio.c:2004-2024` 完全相同（含 `*2` 与 `^0x07` 的次序）。
2. **补齐公式与厂商逐字一致**：先 4 B 对齐、再 `((len+4)/512+1)*512`、恰好 512 整倍数时不补尾巴（`WifiFw.c:330-342` ↔ `aicwf_sdio.c:998-1013`）。这个边界（`len%512==0` 时厂商走 `len=payload_len`）很容易抄错，这里是对的。
3. **流控判据与厂商一致**：先读 fn1 `0x03`，重试 50 次、`<30` 用 200 µs、`<40` 用 2 ms、之后 10 ms，且只在 `len < fc*1536` 时发（`WifiFw.c:287-315,363-369` ↔ `aicsdio.c:693-728`、`:1015-1040`）。
4. **FBR 用 fn0 + 绝对地址、IOEx 只置 fn1 位**：`Sdio.c:986-1013` 与 `sdio_set_block_size`/`sdio_enable_func` 的语义一致（`0x110/0x111` 属 fn0 地址空间）；`NOTES.md:411-428` 记的那次 `OUT_OF_RANGE` 已经修对，别再改回去。
5. **RX 三判据的算术与厂商 IRQ 一致**：`|0x08`、`>120` 分 fn、`127`/`0x78` 字节模式、`(status&0x7F)*512`（`WifiFw.c:470-494` ↔ `aicsdio.c:1505-1545` / `aicwf_sdio.c:2086-2104`）。这是全篇最容易「凭感觉」实现、实际抄得最准的一段。
6. **RX 解析偏移正确**：正文在 `+4`（`id` @+4、`dest` @+6、`src` @+8、`param_len` @+10、`param` @+12）（`WifiFw.c:566-601` ↔ `aicwf_sdio_txrxif.c:211-215` 的 `data+4` 与 `ipc_e2a_msg` 布局）。真机 `hdr=00110014 id=0401` 能和它对上，说明这条没走偏。
7. **1 KB 分块 + 每块等 CFM + `wstatus==0` 才继续**（`WifiFw.c:630-668` ↔ `aic_bsp_driver.c:1233-1278`、`aic_bsp_driver.h:253-262`）。注意厂商上传循环的边界（`size-1024`、尾块）我们做得更直白，等价。
8. **非 D80 硬拒 + 检查/confirm 两段闸**（`WifiFw.c:1046-1068`、`Wifi.c:217-226`）：在「不可逆操作」上分两段，是对的，别因为嫌麻烦合并回一条命令。
9. **枚举失败不拖垮启动盘**：`Sdhci.c:87/101` 无条件探 SD1、失败只打日志、`gWifiSdioState` 只试一次（`Sdio.c:726-730`）。这个「旁路失败不影响启动」的边界要保留。
10. **SDIO 初始化顺序正确**：先卡后主机切 4-bit（`Sdio.c:955-975`）、先 SHS→EHS 再升 50 MHz 再置 `HOST_CONTROL[2]`（`940-950`）、CMD5 前清 `INHIBIT` 门（`Command.c:113-117`）。与 Linux `sdio.c`/`sdhci.c` 的顺序一致。
11. **日志出口统一**：`SdhciMark`/`SdhciMarkHex` 走同一 sink、无 sink 时直写 UART（`Utility.c:82-113`）。这是这一轮真机排障能推进的前提，务必保留。
12. **把 FIFO 回环从默认枚举路径里挪走**（`Sdio.c:1135-1142`）是正确的判断：消息通道不该拿来当回环自检。剩下的问题只是「人工入口还不干净」（§W7）。

---

## 6 · 建议修序 + 硬门禁

### 6.1 修序（按「先让判据可信，再让功能可用」）

| 序 | 改什么 | 涉及 | 出口判据 |
|---|---|---|---|
| **P0-1** | IORx 失败必须红；`Cis` 初始化；`LastStatus/LastR5` 分离 | §W1 §Y9 §Y12 | 人为让 IORX 阶段 CMD52 失败时，日志出现 `FAIL at IORx read`，且不再出现 `fn1 enabled` |
| **P0-2** | `enum ok (cis)` 与 `Ready` 解耦；`chip id` 参与验收；`wififw` 补 ok/FAILED 总结行 | §W2 §Y2 | 拔掉/改坏 MANFID 时 probe 仍能走完，但 shell 打 `probe ok (no identity)` 而不是 `ok` |
| **P0-3** | 检查模式不再碰芯片；`read` 真的读；`\n` 全量修 | §W9 §W10 §Y2 | `wififw write`（无 confirm）日志里不再出现任何 `AicWriteReg`；`read` 有真实字节比对 |
| **P1-1** | `fifotest` 去掉盲读、返回真实结果、结束强制断电 | §W7 | 跑完 `fifotest` 后立刻 `wififw id` 仍能拿到 CFM（证明没污染） |
| **P1-2** | 失败后固定打恢复指引；`write` 增加「上次失败未恢复则拒绝」 | §Y15 | 一次失败后再 `wififw write confirm` 会被拒，并提示 `wifiprobe` |
| **P1-3** | `len==0` 跳过、`Len<12` 判格式错、`Len>512` 读空 | §Y3 §Y5 | 构造畸形帧时日志打 `rx short/long frame`，且下一次 `id` 仍正常 |
| **P2-1** | patch 落点改从表读（`0x001E0000`）；patch 改必需 | §W3 §Y14 | 下载后 `0x001E0000` 读出 `0x681B4B02`，`0x1E7290` 读出 `0x00201940` |
| **P2-2** | 实现 patch_table 的 46 条写（INF/TRAP/PWRON/PATCH_TAF，PWRON 后 `udelay(500)`） | §W4 | `0x40500150` 读到 `1`；`0x001E7294` 读到 `0x001E0000`；TRAP 首条 `0x0020F600` 读到 `0xF0223201` |
| **P2-3** | 实现 `patch_config_8800d80`：读 `0x120198/0x1201A0/0x1201A4/0x12001C` 四个字 → 按版本选 `start_addr` → 写 `patch_tbl` + magic/pair 字段 | §W4 | 日志打出这 4 个读到的值（`0x001C` 应为 `0x06090101`，大于阈值 → 走新映射），并回读 `AIC_PATCH_ADDR(pair_count)` 等于 `patch_cnt`；任何一个是硬编码的都不算过 |
| **P2-4** | `start_app` 后必须以「首条启动消息」为成功判据 | §W5 | `fw state=done` 前面必须有一行 `boot msg` |
| **P3-1** | 真机确认 `>120 → fn2`；未确认前一律 fn1 | §Y6 | 对 fn2 做一次直接 CMD53 读，记录 R5 |
| **P3-2** | 命令超时改为等 `CMD_TIMEOUT`；4-bit 对照先改卡 | §Y11 §Y7 | 慢响应场景下日志出现 `CMD_TIMEOUT` 而不是「无响应」 |

### 6.2 硬门禁（只认这些行；少一行就不算通过）

| 门 | 允许通过的日志（必须逐字出现） | 说明 |
|---|---|---|
| **G0 构建同一性** | `Boot: duo-sdhci build=<DATE> <TIME>` 与 `WiFi: probe start build=<同一串>` | 防「跑的不是这次编的内核」 |
| **G1 枚举** | `WiFi: CIS fn0 ptr=… manfid=c8a1 device=0082` | 必须在场且值精确；缺 MANFID 时 G1 不过，后面全不算 |
| **G2 身份** | `WiFi: chip id=aic8800d80 (manfid=c8a1 pid=0082)` | 由 `Cis.Vendor/Device` 推导，不是常量打印 |
| **G3 通道** | `WiFi: rx masked=09 status=01 fn=01 len=0200 mode=block` **且** `hdr=…0011… id=0401` **且** `cfm.memaddr=40500000` **且** 第二个地址读出不同值 | 后半条是新增的（§W6），没有它 G3 只能算「疑似通」 |
| **G4 写通** | 每阶段 `<name> sent=… size=… ok` + `verify` 通过 + `blk write` 无 `status≠0` | 现有的分阶段行可以保留 |
| **G5 启动** | `start_app cfm bootstatus=…` 之后 **2 s 内** `boot msg type=11/12` | 缺这行 → 不算「固件下载成功」 |
| **G6 可恢复** | 任一失败后出现 `chip left in ROM state; run 'wifiprobe'…` | 保证下一个操作者知道怎么救 |
| **G7 不污染** | `fifotest` 之后紧接的 `wififw id` 仍能拿到 G3 的行 | 证明 §W7 已修 |

### 6.3 真机证伪清单（我可能错的地方）

| # | 我的判断 | 若我错了，应该看到 |
|---|---|---|
| F1 | IORx 读失败会被判成「fn1 enabled」，但**只在 IOEx bit1 已为 1 时**（§W1） | 先正常 `wifiprobe` 一次（IOEx→02），再跑一次 `wifiprobe` 并在 IORX 循环里注入一次 CMD52 失败（临时把 CCCR 地址改错）：现码应仍打 `fn1 enabled`。若它打 `FAIL at fn1 enable timeout (IORx)`，说明我读错了 `break` 后 `Data` 的生命周期；若**首次** probe 就假绿，则说明 IOEx 读那一步的行为和 `NOTES.md:434` 记的不一样 |
| F2 | patch 该落 `0x001E0000`（§W3） | `xxd -s 0x28 -l 32 fw_patch_table_8800d80_u02.bin` 第 4 个字是 `0x001E0000`（已验）；若厂商日志显示 `aicbt_patch_info_unpack` 之后 `addr_patch` 仍是 `0x0020b43c`，或表在运行时被别的表替换，我的结论不成立 |
| F3 | `chip rev raw=40500000` 不可证伪，可能是 memaddr 冒充（§W6） | 加打 `cfm.memaddr` 后应看到 `memaddr=40500000` 且 `memdata` 是**另一个**值（例如 `0x0003xxxx`，对应 `CHIP_REV_U02=3`）；若 `memdata` 仍等于 `40500000`，则要么芯片真这么回，要么 CFM 布局少读 4 B——两种都必须先解决再当判据 |
| F4 | `>120 → fn2` 分支未验证，厂商对 D80 一律读 fn1（§Y6） | 让 `status` 落到 `masked>120`：若 fn2 读能成功并拿到合法帧，我的「可能 FUNCTION_NUMBER」判断不成立；若失败，日志会打 `rx read FAIL` + `r5=…FUNCTION_NUMBER` |
| F5 | 现在 `wififw` 一定在 `read` 阶段就红（§W8） | 若真机能打出 `WiFi: fw read /Assets/Fw/aic8800/fw_adid.bin 1708B ok`，说明盘上已有这套文件（`Runtime/RootFs/RiscV/Assets` 与 `Runtime/Assets` 里都没有，但 SD 卡可能是手工放的），那我这条作废 |
| F6 | `0x01` 不拦字节模式（§I5） | 若某次真机字节模式 CFM 读失败而块模式成功，先查 `0x07` 这一位，再查 `Padded<=512` |

### 6.4 `Sdio.c` 拆分边界（按职责，不按行数）

现在 1154 行里混了 5 种职责。最该先切的一刀不是「枚举 vs 自检」，而是**把测量代码从状态机里赶出去**，因为状态机的每一行现在都被诊断代码的返回值/副作用牵连（§W1 §W7 都是这么来的）。

| 目标文件 | 搬走什么 | 当前行 | 理由 |
|---|---|---|---|
| `SdioCis.c` | `SdioReadCisPointer` / `SdioParseCis` / `SdioScanCis` / `SdioMarkCis` | `67-199` | 纯协议解析、无 MMIO 副作用，可在 x86 上用假卡跑（`NOTES.md:449-450` 已有这类 harness），最值得加断言 |
| `SdioDiag.c` | `SdioFifoTest` / `SdioDatReadCheck` / `SdioCmpCisRead` / `SdioMarkXfer` / `SdioMarkErrBits` / `SdioMarkFn1Regs` / `SdioMarkCmd5State` / `SdioDumpCis` | `148-481`、`539-576` | 一次性排障工具，**默认不参与任何判定**；放独立文件后，`Sdio.c` 里就不可能「顺手」用它的返回值改状态 |
| `SdioTransport.c` | `SdioRecoverBus`（与 `Io.c` 的 CMD52/53 合并），`SdhciWifiBusRecover` | `287-315`、`702-707` | 复位/恢复属于传输层，不属于枚举；消息层（`WifiFw.c`）只应依赖它 |
| `WifiHost.c` | `gWifiSdioState`、`SdhciWifiProbe`、`SdhciWifiReady`、`SdhciWifiChipIsD80`、`SdhciWifiFifoTest`（薄壳） | `57-65`、`641-707` | 对外只有这一个门面；状态从 `0/1/-1` 改成 enum（`NONE/PROBED/READY/FAILED`），并把「身份」与「总线」分成两个状态位（§W2） |
| `SdioInit.c` | `SdioCmd5Window` + `SdhciWifiInitialize` 的 CMD0/5/3/7、CCCR/HS/4-bit、fn1 门 | `586-640`、`709-1013` | 纯顺序状态机，一步一行判据；不应出现 hexdump/自检分支 |

同时 `WifiFw.c`（1081 行）也该按同一原则切：`AicMsg.c`（帧编解码 + 通道 + 流控 + pending，`322-605`）、`AicFwImage.c`（分块读 + 校验 + 阶段表，`630-1001`）、`WifiFw.c`（只剩门禁与编排，`1003-1081`）。切完后再做 §6.1 的 P2，改动面会小一半。

---

*评审完。所有行号对应 §1 表格里的快照；若文件已被继续修改，请以 md5 为准重新对行。*
