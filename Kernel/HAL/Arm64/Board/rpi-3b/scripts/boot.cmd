# Blocks rpi-3b — U-Boot script
# 对齐 X64：固件在 mmc 0:1 (bootfs)；Kernel.elf 在 mmc 0:2 (BLOCKS FAT32)
# bootelf 入口是 (argc,argv)，x0≠DTB → 内核读固定址 BOARD_DTB_LOAD=0x02400000
# smsc95xx_eth address not set：U-Boot 扫 USB 网时缺地址；脚本里补上（早于 bootelf）。
# 若报错出现在 source boot.scr 之前（distro USB 扫描），属 U-Boot 固有，可忽略或
# 提示符下：setenv usbethaddr b8:27:eb:00:00:01 ; setenv ethaddr b8:27:eb:00:00:01 ; saveenv
setenv usbethaddr b8:27:eb:00:00:01
setenv ethaddr b8:27:eb:00:00:01
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
