# Ksend — 串口增量写任意文件（Duo S 开发期工具，v5）

目标：改完内核不用拔卡，PC 只发和卡上不一样的那几块，8 秒级一轮，写完自动复位。
v2 起目标不再写死 `Kernel.elf`：`--file` 是本地文件，`--path` 是卡上目标，
板端对应 `ksend <路径>`；两边都不给路径时仍是 `BLOCKS:Kernel.elf`。
v3 起 `BOOT:` 指向启动分区（板端按卷选择后放行写只读的 boot 卷），
`--no-reboot` 让板子校验通过后留在 shell（一键脚本用）；默认仍自动复位。
v4 起 `--no-backup` 跳过写前 `.bak`（boot.sd 这类可再生成的目标用），
串口明确打 `backup: skipped (--no-backup)`；跳过时失败不回滚。
v5 起在目标旁维护 `<目标全名>.crc` 差量表：`--no-backup` 部署时 READY 之前
只读这张 2 KB 的表，不再整读 2 MB 目标文件（Duo 的 SDIO 是 512 B 一块，
整读一次要几千次往返，4~10 分钟）。见下面「差量缓存」一节。

```
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file Kernel/Build/DUO/HAL/RiscV/Kernel.elf
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file SNAKE.ELF --path BLOCKS:SNAKE.ELF
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file boot.sd --path BOOT:boot.sd
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file boot.sd --path BOOT:boot.sd --no-reboot
PC:  python3 Tools/ksend.py --port /dev/ttyUSB0 --file boot.sd --path BOOT:boot.sd --no-backup
板:  blocks> ksend info               # 只读：Kernel.elf 的 size / n / 整体 CRC（行为不变）
板:  blocks> ksend selftest           # 只写 8 KB 临时文件，判 FAT 写路径（行为不变）
板:  blocks> ksend                    # 完整传输，写 Kernel.elf（回归）
板:  blocks> ksend BLOCKS:SNAKE.ELF   # 完整传输，写指定文件
板:  blocks> ksend BOOT:boot.sd       # 写启动分区上的 boot.sd
板:  blocks> ksend --no-reboot BOOT:boot.sd   # 同上，校验通过后留 shell
板:  blocks> ksend --no-backup BOOT:boot.sd   # 跳过写前 .bak（失败不回滚）
```

一键（build + 生成 FIT + 传 BOOT:boot.sd + 提示可复位，不自动复位）：

```
source ~/Blocks/env.sh
deploy.sh --port /dev/ttyUSB0          # 脚本在 scripts/deploy.sh
```

## 线上格式（小端）

| 方向 | 内容 |
| ---- | ---- |
| PC → 板 | `ksend\n`（默认路径）或 `ksend [--no-reboot] <目标路径>\n`（板端 shell 参数） |
| 板 → PC | `KSEND READY size=<旧大小> block=4096 n=<旧块数>\r\n` |
| 板 → PC | n 个 `u32` 旧块 CRC32，然后 1 字节 `0x0A` |
| PC → 板 | 每个差异块：`u32 magic=0x4B53424B` `u32 index` `u32 len` `u32 crc32(data)` + data |
| 板 → PC | `'A'` 已收进批缓冲（512 KB 一批落盘） / `'N'` 请重发同一块（PC 最多重发 3 次） |
| PC → 板 | 收尾：`u32 magic` `u32 0xFFFFFFFF` `u32 0` `u32 新文件整体 CRC32` |
| 板 → PC | `KSEND OK crc=… written=… size=…`（默认随后复位；`--no-reboot` 留 shell）或 `KSEND FAIL …`（不复位） |

CRC 是 CRC-32/ISO-HDLC，和 Python `zlib.crc32` 同参。

## 差量缓存 `<目标全名>.crc`（v5）

路径与目标同卷同目录，名字 = 目标全名追加 `.crc`（`BOOT:boot.sd` →
`BOOT:boot.sd.crc`，`BLOCKS:Kernel.elf` → `BLOCKS:Kernel.elf.crc`）。内容小端，
字段定长（`u32`）：

| 偏移 | 字段 | 值 / 含义 |
| ---- | ---- | --------- |
| 0 | `magic` | `0x4B534352`（协议给定值，按小端写出字节为 `52 43 53 4B`） |
| 4 | `version` | `1` |
| 8 | `block_size` | `4096` |
| 12 | `block_count` | `ceil(total_size / 4096)` |
| 16 | `total_size` | 目标文件当前大小 |
| 20 | `full_crc` | 整个文件的 CRC32（传输收尾帧里那个值） |
| 24 | `block_crc[]` | 每 4 KB 一块的 CRC32，共 `block_count` 个 |

文件大小恒为 `24 + 4 × block_count`（517 块 ≈ 2 KB）。

**使用规则（READY 之前）**：

1. `stat` 目标得到 `OldSize`；`stat` `.crc` 得到实际大小。
2. 只有同时满足下面全部条件才用 `.crc` 的块表，且**不读目标文件**：
   magic/version/block_size 对；`total_size == OldSize`；`block_count` 与
   `total_size` 自洽；`.crc` 实际大小 == `24 + 4×block_count`；`block_count`
   不超过上限。任何一条不满足 → 退回逐块读目标文件的慢路径（正确但慢）。
3. 缓存只在**整文件读回校验通过之后**刷新；`KSEND FAIL`、回滚、abort 都不写。
4. 新文件比旧文件小时，FAT 没有 truncate，目标目录项尺寸仍是旧值；此时
   缓存既不能写「新尺寸」（永远不自洽），也不能留旧表（大小没变会被当成
   有效表，但内容已变），所以**删掉旧缓存**（串口打
   `cache: dropped (target keeps old size)`），下一次走慢路径。等文件不再
   缩小时缓存会重新建立。
5. `full_crc` 会随缓存写/读，但 READY 之前板端没有独立来源能核对它（不读
   文件就无法确认），所以它**不参与差量判断**；改它不会让缓存失效，下一次
   成功传输会重新写对。真正决定「发哪些块」的是 `block_crc[]`。

**安全边界（必须知道）**：

- `.crc` 只是**加速缓存**，不是事实来源；它**只在目标文件由 ksend 自己写过
  （并读回校验通过）之后才可信**。任何外部写入——读卡器刷盘、`ck-duo-sd.sh`、
  别的命令改文件——都必须让对应的 `.crc` 失效：删掉它。删掉后下次走慢路径，
  结果不变，只是慢。
- 不读文件时，板端能 O(1) 确认的只有 `total_size`（目标 `stat` 大小）；
  `full_crc` 无法在不读文件的前提下与目标内容核对，只能信任「它是上次
  校验通过后自己写的」。因此**外部写入会绕过它**：读卡器刷了卡、或别的
  命令改了目标而 `.crc` 没变，缓存就过期了。
- 兜底有两层：
  1. `scripts/ck-duo-sd.sh` 在**写目标之前**调用 `drop_stale_cache`：
     - `$BootMnt/boot.sd.crc`（boot 分区的 `BOOT:boot.sd.crc`）
     - `$BlocksMnt/Kernel.elf.crc`（BLOCKS 分区的 `BLOCKS:Kernel.elf.crc`）

     删完会检查文件确实不存在；删不掉就报错退出，绝不带着过期块表继续。
     可操作检查：跑完 `ck-duo-sd.sh` 后，`test ! -e "$BootMnt/boot.sd.crc"`
     与 `test ! -e "$BlocksMnt/Kernel.elf.crc"` 都应成立；串口/脚本日志里
     会分别出现 `- boot boot.sd.crc (stale cache)` /
     `- BLOCKS Kernel.elf.crc (stale cache)`（原本不存在则不打）。
  2. 每次传输收尾仍会用**整文件读回**逐块比对预期。过期缓存最多让这次
     部署 FAIL（`--no-backup` 时目标可能半新半旧，需要重刷），**不会**在
     `KSEND OK` 的前提下把目标写错：未发送的块必须与旧表一致，发送的块
     已被新数据覆盖；只有整份内容恰好等于 PC 的本地文件时读回校验才会过。
     一旦这次部署失败且已经写过卡（`Dirty`），板端会删掉 `.crc`
     （`cache: dropped (failed write; next transfer scans)`）：回滚成功也删，
     因为无法证明旧表没被外部改过。下一次慢扫重建，慢但确定正确。
- 如果将来去掉整文件读回校验，这个前提就破了，过期缓存会变成静默写错。
  删 `.crc` 永远是最省事的「回到已知正确」手段。

## 安全顺序（顺序不能换）

1. `ksend selftest` 先过：8 KB 临时文件，新建一遍 + 覆写一遍，读回逐字节比对，删除。
2. 真传第一步：`<目标>` → 同目录 `<目标全名>.bak`（512 KB 分块走 FileSystem API），
   回读校验 size + 整体 CRC。备份不过就 `KSEND FAIL reason=backup`，一个字节都不写。
   备份名规则：目标全名追加 `.bak`（`Kernel.elf` → `Kernel.elf.bak`，
   `boot.sd` → `boot.sd.bak`），保留原大小写；boot 分区的救生圈就是它。
   `--no-backup` 时跳过复制：优先读同目录 `<目标全名>.crc` 的旧块表（命中则
   不读目标文件）；缺失/不自洽才逐块读旧内容。串口打
   `ksend: backup: skipped (--no-backup)`；失败时打 rollback: skipped，不假装回滚。
3. 选卷：`FileSystemResolve` 解出目标卷，`FileSystemSelectVolume` 选中
   （boot/ESP 被挂载策略标成只读时，选中即放行写；用完 `SelectVolume(-1)` 归还）。
4. 直接覆盖写目标文件（PC 按 4 KB 块落在 `index*4096`）。板端把连续的块攒进
   512 KB 批缓冲，满一批/遇到不连续/收尾帧时一次 `WriteFileAt`，然后回 `'A'`。
   逐块 4 KB 写会让 FAT 每次从首簇走到偏移（第二层 O(n²)），批量写是必须的。
5. 收尾：`FileSystemFileSync`（→ `BlockFlush`）后重新打开文件逐块读回，
   块 CRC 对齐预期、整体 CRC 对齐收尾帧。只有读回通过才刷新 `.crc`
   （`cache: updated`）；失败/回滚/abort 都不碰它。
6. 只有校验通过才 `KSEND OK` 并（默认）复位；任何失败都 `KSEND FAIL`，**不复位**；
   只要尝试过写卡（含写失败）且没加 `--no-backup`，就用备份删了重写并回读校验，
   打印 `KSEND RESTORED`；`--no-backup` 时只打 `rollback: skipped (--no-backup)`。
   失败且写过卡时删掉 `.crc`（回滚成功也删），下一次慢扫重建。

## v1–v5 与需求文本的出入（都是刻意的）

1. **收尾帧没有 size 字段**，板子只能用「收到过的最大 `index*4096+len`」推新大小。
   因此 PC 侧**永远重发最后一块**（哪怕它没变）；否则「新文件变短 / 末块没变」
   时会把新尺寸推小。PC 与板子都按这条写。
2. **多了 abort 帧**：`index=0xFFFFFFFE, len=0`。只给 `--dry-run` 和 Ctrl-C 用，
   让板子立刻收手（没写过卡 → `KSEND ABORT`；写过 → 先回滚再 ABORT）。
   协议里本来没有「PC 不发了」这个信号。
3. `KSEND FAIL` 只在块 CRC 不符时用 `at=/want=/got=`；I/O 失败用
   `reason=write at=<块> err=<代码>`——此时 want 与 got 相等，套规范那三个字段反而误导。
4. `info` / `selftest` 额外打一行人类可读文本；机器行分别是
   `KSEND INFO size=… block=4096 n=… crc=…` 和 `KSEND SELFTEST OK|FAIL …`。
5. 路径只走**命令参数**，不塞进二进制帧：PC 发的 `ksend <路径>` 就是板端
   shell 命令行。因此路径里不能有空格/控制字符，目标长度上限 91
   （板端 96 B 缓冲，还要放得下 `.bak`）。`--info` 仍只查 `Kernel.elf`（行为不变）。
6. **没有原子写**：FAT 层没有 rename，boot.sd 是原地覆盖。写坏/掉电后卡上
   boot.sd 可能半新半旧 → 下次上电不进内核 → 读卡器重刷（`.bak` 可改名回去）。
   这是刻意的取舍：不为掉电再加一套 new+rename。

## 已知限制

- **FAT 层没有 truncate**（只调 `FileSystem.h` 公开 API）。新文件比旧文件小时，
  [新大小, 旧大小) 会被写成 0，但目录项尺寸仍是旧值：目标文件带 0 尾巴。
  ELF 装载按 program header 取，尾巴不参与；FIT 按头部 totalsize 取，也不参与。
  下一次 `ksend info` 看到的是旧尺寸，下次传输也照常工作（多出来的块永远是 0）。
  真要精确尺寸只能整文件重写。
- 单文件上限 `FAT_WRITE_MAX` = 8 MB（FAT 层限制）。
- `.crc` 只用 `stat` 确认「大小没变」；内容被外部改成同样大小时它在 READY
  之前发现不了。它只对「ksend 自己写过的目标」可信，外部写入后必须删
  `.crc`（`ck-duo-sd.sh` 已自动做）；兜底是收尾的整文件读回校验（结果会是
  FAIL，不是 OK）。
- v5 只去掉 READY 之前的整文件扫描；收尾仍要整文件读回校验（安全兜底），
  所以传输总耗时仍包含一遍 2 MB 读，具体见真机日志。缓存命中时 READY 之前的
  部分只剩读 2 KB 的 `.crc`。
- `'A'` 在 v4 起表示「已收进 512 KB 批缓冲」，不再等于「已落盘」；整文件
  `FileSystemFileSync` + 读回 CRC 仍在校验通过前完成，失败会回滚（除非 --no-backup）。
- 串口固定 115200：内核不重设波特率，U-Boot 设成什么就是什么。
- 传输窗口内掉电会留下「半新半旧」的目标文件：板子自己起不来，只能读卡器
  把 `<目标全名>.bak` 改名回去。备份是唯一的救生圈，别删。
- `BOOT:` 只认挂载时探测到 `boot.sd`/`boot.scr`/`fip.bin` 等启动文件的卷；
  卡上连这些都没有时 `BOOT:` 解析失败（`KSEND FAIL reason=stat err=-2`）。
