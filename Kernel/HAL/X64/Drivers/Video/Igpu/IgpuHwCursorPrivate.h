/*
 * IgpuHwCursorPrivate.h — hw-cursor 门面内部分发（IgpuHwCursor / Ddb / Buffer）
 *
 * 契约与寄存器口径见 IgpuHwCursor.c 文件头；勿改 DDB 割块 / CUR 武装序。
 */
#ifndef IGPU_HW_CURSOR_PRIVATE_H
#define IGPU_HW_CURSOR_PRIVATE_H

#include "BootTypes.h"

#define IGPU_CUR_GTT_OFF       0x05900000ull
#define IGPU_CUR_SIZE          64u
#define IGPU_CUR_BYTES         (IGPU_CUR_SIZE * IGPU_CUR_SIZE * 4u)
#define IGPU_CUR_PAGES         ((IGPU_CUR_BYTES + 4095u) / 4096u)
/* i915 skl_cursor_allocation：单 active pipe 至少 32 blocks */
#define IGPU_CUR_DDB_BLOCKS    32u

#define IGPU_REG_CUR_CTL_A     0x70080u
#define IGPU_REG_CUR_BASE_A    0x70084u
#define IGPU_REG_CUR_POS_A     0x70088u
#define IGPU_REG_CUR_FBC_CTL_A 0x700A0u
#define IGPU_REG_CUR_BUF_CFG_A 0x7017Cu
#define IGPU_REG_CUR_WM_A0     0x70140u
/* PLANE_1（primary）BUF/WM；与 PLANE_CTL 0x70180 同管 */
#define IGPU_REG_PLANE_BUF_1_A 0x7027Cu
#define IGPU_REG_PLANE_WM_1_A0 0x70240u
#define IGPU_REG_PLANE_SURF_B  0x7119Cu
#define IGPU_REG_PLANE_SURF_C  0x7219Cu

#define MCURSOR_MODE_64_ARGB_AX 0x27u
#define CURSOR_POS_X_SIGN      (1u << 15)
#define CURSOR_POS_Y_SIGN      (1u << 31)
#define CUR_WM_EN              (1u << 31)
#define PLANE_WM_EN            (1u << 31)
#define PLANE_WM_BLOCKS_MASK   0x1FFFu
#define CUR_WM_BLOCKS_MASK     0xFFFu
#define BUF_START_MASK         0xFFFu
#define BUF_END_SHIFT          16
#define BUF_END_MASK           0xFFFu

extern int gCapable;
extern int gProbed;
extern int gOn;
extern int gDdbReady;
extern UINT32 *gArgb;
extern UINT64 gPhys;
extern UINT32 gHotX;
extern UINT32 gHotY;
extern UINT32 gCurCtl;
extern UINT32 gCurBase;
extern UINT32 gCurPos;
extern UINT32 gCurFbc;
extern UINT32 gCurrentBufferConfig;
extern UINT32 gCurWm0;
extern UINT32 gPlaneBufReg;
extern UINT32 gPlaneWm0Reg;
extern UINT32 gSavedBufferConfig;
extern UINT32 gSavedWm0;
extern UINT32 gOrigPlaneBuf;
extern int gCarved;

int EnsureCursorDdb(void);
void RestorePlaneDdb(void);
int EnsureBuffer(void);
void CopyGlyph(const UINT32 *Src, UINT32 Width, UINT32 Height, UINT32 *Destination);

#endif
