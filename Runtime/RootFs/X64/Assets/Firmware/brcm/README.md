# brcm — Pi 3B WiFi 固件（pwifi-2）

来源：主机 `/lib/firmware/brcm/`（linux-firmware / RPi），解压后落入本目录。

| 文件 | 用途 |
| --- | --- |
| `brcmfmac43430a0-sdio.bin` | BCM43430 **rev=1**（A0） |
| `brcmfmac43430-sdio.bin` | BCM43430 **rev≥2**（A1；常见 3B） |
| `brcmfmac43430-sdio.txt` | NVRAM（自 `…raspberrypi,3-model-b.txt`；FAT 短名） |
| `brcmfmac43430-sdio.clm_blob` | CLM（后刀可选） |

`ck-pi-sd` 会把本目录拷到 BLOCKS 卷 `Assets/Firmware/brcm/`。
