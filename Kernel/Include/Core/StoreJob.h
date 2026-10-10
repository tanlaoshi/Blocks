/*
 * StoreJob.h — K48：装包作业互斥（薄）
 *
 * 【初学者】
 * Shell 与 Store 窗共用忙旗；Install 入队，Step 执行拷贝。
 * 不做现网 WorkerTask 后台任务名。
 */
#ifndef STORE_JOB_H
#define STORE_JOB_H

#include "Store.h"

#define STORE_BUSY (-5)

int StoreJobIsBusy(void);
int StoreJobBegin(void);
void StoreJobEnd(void);
/* 入队；Busy 则 STORE_BUSY；成功排队返回 STORE_OK */
int StoreJobInstall(const char *Id);
/* 谁调用：Console/Gui 轮询。有作业则执行一次 StoreInstall，返回 1 */
int StoreJobStep(void);
int StoreJobLastError(void);

#endif
