/*
 * Ksend.h — 串口增量写卡门面（三架构 Common）
 *
 * 人话：不拔卡，PC 只发和卡上不一样的块，写完可选复位。
 * 实现：HAL/Common/Ksend/；壳 Commands/Ksend.c 弱引用本三符号。
 */
#ifndef HAL_KSEND_H
#define HAL_KSEND_H

int KsendInfo(void);
int KsendSelftest(void);
int KsendTransfer(const char *TargetPath, int Reboot, int Backup);

#endif /* HAL_KSEND_H */
