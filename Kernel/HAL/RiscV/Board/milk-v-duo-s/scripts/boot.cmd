# Blocks Duo S - U-Boot script (mkimage -> boot.scr)
# Load ELF to vendor uImage_addr; PHDR entry 0x80200000.
# Do not strip FSBL / fip.bin / boot.sd from this FAT.
fatload mmc 0:1 0x81800000 Kernel.elf
bootelf 0x81800000
