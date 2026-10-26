# Blocks Duo S - U-Boot script (mkimage -> boot.scr)
# Align X64/Pi: Kernel.elf on LABEL=BLOCKS (mmc 0:4 on vendor+BLOCKS layout).
# boot (mmc 0:1) keeps fip.bin / boot.sd / duo.dtb - do not strip FSBL.
# a0=hartid a1=DTB via bootelf -p (duo-dtb).
fatload mmc 0:4 0x81800000 Kernel.elf
if fatload mmc 0:1 0x81200000 duo.dtb; then
	fdt addr 0x81200000
	bootelf -p 0x81800000
else
	bootelf 0x81800000
fi
