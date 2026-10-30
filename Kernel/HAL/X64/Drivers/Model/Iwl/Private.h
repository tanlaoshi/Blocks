/*
 * Private.h — 薄转发。原有 .c 仍包含本文件，一次看见共享状态、Hw、Mac。
 */
#ifndef IWL_PRIVATE_H
#define IWL_PRIVATE_H

#include "Iwl/IwlState.h"
#include "Hw/IwlHwPrivate.h"
#include "Mac/Private.h"

/* 密码学原语已搬到 HAL/Common/WifiCrypto：SHA1/PBKDF2/PRF/AES/EAPOL 都在那边，
 * 驱动只保留 IwlInstallGtk 这份串口/全局状态胶水（见 Mac/Eapol/Crypto.c）。 */
int IwlCcmpEncrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);
int IwlCcmpDecrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);
int IwlTkipDecrypt(const UINT8 Tk[32], UINT8 *Frame, UINTN HdrLen, UINTN CryptLen);
int IwlTkipMichaelOk(const UINT8 Tk[32], const UINT8 *Frame, UINTN HdrLen,
                     const UINT8 *Payload, UINTN DataLen);

#endif /* IWL_PRIVATE_H */
