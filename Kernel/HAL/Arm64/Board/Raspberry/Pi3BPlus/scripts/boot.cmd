# Blocks Pi3BPlus — U-Boot script
# 对齐 X64：固件在 mmc 0:1 (bootfs)；Kernel.elf 在 mmc 0:2 (BLOCKS FAT32)
# bootelf 入口是 (argc,argv)，x0≠DTB → 内核读固定址 BOARD_DTB_LOAD=0x02400000
load mmc 0:2 0x20000000 Kernel.elf
if test -n "${fdt_addr}"; then
	fdt addr ${fdt_addr}
	fdt move ${fdt_addr} 0x02400000
elif test -n "${fdtcontroladdr}"; then
	fdt addr ${fdtcontroladdr}
	fdt move ${fdtcontroladdr} 0x02400000
else
	load mmc 0:1 0x02400000 pi.dtb
	fdt addr 0x02400000
fi
bootelf -p 0x20000000
