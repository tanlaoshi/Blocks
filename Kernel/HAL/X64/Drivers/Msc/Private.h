/*
 * Private.h — BOT 命令块和状态包装的字节布局。
 * xHCI 与 EHCI 都用这一份。传输、锁、清 Halt 留在各自文件。
 */
#ifndef MSC_PRIVATE_H
#define MSC_PRIVATE_H

#include "BootTypes.h"

/* 写入 32 字节。线上只发前 31 字节，最后一字节保持 0。 */
void CommandFill(UINT8 *Cbw, UINT32 Tag, const UINT8 *CbwCb, UINT8 CbLen,
                 UINT32 DataLen, int DataIn);

/* 1 = 签名是 USBS。状态字节仍由调用方看 Csw[12]。 */
int StatusSignature(const UINT8 *Csw);

#endif
