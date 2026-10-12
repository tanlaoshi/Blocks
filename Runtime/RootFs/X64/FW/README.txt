ToyOS FW/ — wireless firmware + Wi-Fi config (classroom)

IWL8265.UCODE
  Chip: Intel Wireless 8265/8275 (PCI 8086:24fd)
  Source: host /lib/firmware/intel/iwlwifi/iwlwifi-8265-36.ucode.zst
          (linux-firmware; decompressed, API rev 36)
  Size: ~2.3 MiB
  SHA256: 1336afcd028ed094d1fe33893c84c273bb5711be52970040344a75a12f276d56
  License: Intel redistributable firmware via linux-firmware (NOT ToyOS code).
           Do not claim as original; keep this notice with the blob.
  Guest path: FW/IWL8265.UCODE  (FileSystemReadFile)

WIFI.CFG  (PR-N-wifi-2)
  Guest path: FW/WIFI.CFG  (kernel only reads this name)
  Format (THEME.CFG style, one key per line):
    SSID=your-ap-name
    PSK=your-wpa2-passphrase
  Example only (no real secrets): WIFI.CFG.example
  Real profiles stay on the USB stick — do not commit passwords.
  Suggested stick layout (PSK never in git):
    FW/WIFI_H.CFG  — home AP
    FW/WIFI_C.CFG  — company / classroom AP
    FW/WIFI.CFG    — active copy:  cp WIFI_H.CFG WIFI.CFG   (or WIFI_C)
  sync-usb preserves FW/WIFI.CFG and FW/WIFI_[HC].CFG on TOYOS.
  Missing CFG / bad PSK / no AP → soft-fail; desktop OK; no NetAttachNic.
  Driver never prints PSK to serial.

Driver: HAL/X64/Drivers/Iwl (iwl8265). Missing ucode → Probe soft-fail.
sync-usb.sh copies the whole FW/ tree next to Kernel.elf.
