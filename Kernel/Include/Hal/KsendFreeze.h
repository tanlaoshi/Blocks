/*
 * KsendFreeze.h — 热更冻结钩子（Common 不链 Gui）
 *
 * Begin：NUC/Pi 由 Desktop 登记大窗；各板串口一行「热更中」。
 * End：解锁输入、重绘桌面（成功且复位则可不调）。
 */
#ifndef HAL_KSEND_FREEZE_H
#define HAL_KSEND_FREEZE_H

typedef struct {
    void (*Begin)(void);
    void (*End)(void);
} KSEND_FREEZE_OPS;

void KsendFreezeOpsRegister(const KSEND_FREEZE_OPS *Operations);
void KsendFreezeBegin(void);
void KsendFreezeEnd(void);

#endif /* HAL_KSEND_FREEZE_H */
