# Blocks Pi3BPlus — U-Boot script（少打字）
# 地址写死，不依赖 env（bad CRC 时仍可用）
load mmc 0:1 0x20000000 Kernel.elf
load mmc 0:1 0x02400000 pi.dtb
fdt addr 0x02400000
bootelf -p 0x20000000
