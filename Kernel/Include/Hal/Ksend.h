/*
 * Ksend.h — 增量写卡门面（三架构 Common）
 *
 * 人话：不拔卡，PC 只发和卡上不一样的块，写完可选复位。
 * 传输：串口（备份）或 TCP（网络热更主路；有线/Wi‑Fi 同 listen）。
 * 实现：HAL/Common/Ksend/；壳 Commands/Ksend.c 弱引用。
 */
#ifndef HAL_KSEND_H
#define HAL_KSEND_H

#include "BootTypes.h"

typedef enum {
    KSEND_SERVE_IDLE = 0,
    KSEND_SERVE_LISTENING = 1,
    KSEND_SERVE_TRANSFERRING = 2
} KSEND_SERVE_STATE;

int KsendInfo(void);
int KsendSelfTest(void);
int KsendTransfer(const char *TargetPath, int Reboot, int Backup);
/* 阻塞封装：ListenStart 后泵 Poll 直到 Idle（壳用） */
int KsendServe(UINT16 Port, UINT32 WaitMilliseconds);
UINT16 KsendServeDefaultPort(void);

/* 后台听：Settings / Worker 泵 Poll；Stop 可取消听 */
int KsendServeListenStart(UINT16 Port, UINT32 WaitMilliseconds);
void KsendServeListenStop(void);
void KsendServePoll(void);
KSEND_SERVE_STATE KsendServeState(void);
int KsendServeLastResult(void);
UINT16 KsendServeListenPort(void);
void KsendServeRequestAbort(void);

UINT16 KsendServePreferencePort(void);
void KsendServePreferenceSetPort(UINT16 Port);
int KsendServePreferenceReboot(void);
void KsendServePreferenceSetReboot(int On);
int KsendServePreferenceBackup(void);
void KsendServePreferenceSetBackup(int On);

#endif /* HAL_KSEND_H */
