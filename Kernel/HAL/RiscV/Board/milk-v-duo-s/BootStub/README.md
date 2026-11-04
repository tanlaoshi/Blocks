# Duo BootStub（第二阶段）

FIT `boot.sd` 内嵌本 ELF（`@0x83000000`）。U-Boot `bootm` 后：

1. 初始化 UART0 + SD0  
2. GPT 扫 FAT，认根目录 `BLOCKS.ID`  
3. **ksend-rollback**：`boot.try` +1；若计数 >3 且存在 `Kernel.elf.bak` → 改载 bak，否则 `Kernel.elf` → `0x84000000`  
4. 装 PT_LOAD → `0x80200000`，`a0=hartid` `a1=DTB` 跳转  
5. 真核 `FS: Ready` 后写 `B:boot.try=0` 清试跑计数  

```bash
./build.sh
python3 ../scripts/mk-boot-sd.py --stub -o ../scripts/boot.sd
```

真核链接地址不变。stub 坏 → 读卡器重刷 `boot.sd`（保留 `boot.sd.vendor`）。  
Pi / 无 BootStub 板：本刀只落地 Duo；`.bak` 仍由 ksend 写前备份。
