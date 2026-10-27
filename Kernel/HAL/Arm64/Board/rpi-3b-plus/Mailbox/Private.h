/*
 * Mailbox/Private.h — Pi VC mailbox 内部（bd-pi-sd 顺手）
 */
#ifndef BOARD_MAILBOX_PRIVATE_H
#define BOARD_MAILBOX_PRIVATE_H

#include "Board.h"
#include "BootHandoff.h"
#include "BootTypes.h"
#include "Hal.h"
#include "HalSerial.h"

#define MBOX_BASE            0x3F00B880ULL
#define MBOX_RD              0x00
#define MBOX_STATUS0         0x18
#define MBOX_WR              0x20
#define MBOX_STATUS1         0x38

#define MBOX_FULL            0x80000000u
#define MBOX_EMPTY           0x40000000u
#define MBOX_CHAN_PROP       8u
#define MBOX_RESP_SUCCESS    0x80000000u

#define MBOX_TAG_GET_ARM_MEM 0x00010005u
#define MBOX_TAG_SET_POWER   0x00028001u
#define MBOX_TAG_GET_CLOCK   0x00030002u
#define MBOX_TAG_ALLOC_BUF   0x00040001u
#define MBOX_TAG_GET_PHY_WH  0x00040003u
#define MBOX_TAG_SET_PHY_WH  0x00048003u
#define MBOX_TAG_GET_VIRT_WH 0x00040004u
#define MBOX_TAG_SET_VIRT_WH 0x00048004u
#define MBOX_TAG_SET_DEPTH   0x00048005u
#define MBOX_TAG_SET_PIXORD  0x00048006u
#define MBOX_TAG_GET_PITCH   0x00040008u
#define MBOX_TAG_GET_VOFF    0x00040009u
#define MBOX_TAG_SET_VOFF    0x00048009u

#define MBOX_POWER_ON_WAIT   3u /* ON|WAIT */

/*
 * 固件：0=BGR，1=RGB。本仓 UI 色为 0x00RRGGBB（低字节=B），与 X64 GOP BGR 同形。
 * 若设 RGB，COLOR_BLUE(0xFF) 会画成红（3B+ 桌面「设蓝见红」）。
 */
#define MBOX_PIXORD_BGR      0u
#define MBOX_PIXORD_RGB      1u

#define PHYS_TO_BUS(P)       (((UINT32)(P) & 0x3FFFFFFFu) | 0xC0000000u)
#define BUS_TO_PHYS(B)       ((UINT32)(B) & 0x3FFFFFFFu)

#define MBOX_SPIN_MAX        10000000u

#define FB_FALLBACK_W        1280u
#define FB_FALLBACK_H        720u
#define FB_REQ_BPP           32u
#define FB_ALIGN             4096u

volatile UINT32 *MailboxRegister(UINT32 Offset);
void MailboxDataSynchronizationBarrier(void);
void MailboxBootHexadecimal32(UINT32 Value);
int MailboxProperty(UINT32 *Buffer, UINT32 WordCount);

#endif
