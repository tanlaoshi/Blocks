# intel — NUC 板载 USB HCI 固件（BTM）

来源：主机 `/lib/firmware/intel/`（linux-firmware），`zstd -d` 后落入本目录。

| 文件 | 用途 |
| --- | --- |
| `ibt-12-16.sfi` | 8087:0A2B WsP（hw_variant=0x0c，dev_revid=16）bootloader → operational |
| `ibt-12-16.ddc` | 设备配置（可选；无文件仍可进 operational） |

路径（NUC 运行时）：`FW/ibt-<hw_variant>-<dev_revid>.sfi`（对表 btintel SfP/WsP；与 `FW/IWL8265.UCODE` 同级）。  
仓内源：本目录；同步时拷到 RootFs `FW/`（及可选 `Assets/Firmware/intel/`）。
