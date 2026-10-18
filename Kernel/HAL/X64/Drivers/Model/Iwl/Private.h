/*
 * Private.h — 薄转发。原有 .c 仍包含本文件，一次看见共享状态、Hw、Mac。
 * 密码学声明留在这里：Crypto 既不是 Hw 也不是 Mac。
 */
#ifndef IWL_PRIVATE_H
#define IWL_PRIVATE_H

#include "Iwl/IwlState.h"
#include "Hw/IwlHwPrivate.h"
#include "Mac/Private.h"

int IwlAesEncrypt(const UINT8 Key[16], const UINT8 In[16], UINT8 Out[16]);
void IwlAesKeyExpand(const UINT8 Key[16], UINT8 Rk[176]);
void IwlAesAddRoundKey(UINT8 S[16], const UINT8 *Rk);
int IwlAesDecrypt(const UINT8 Key[16], const UINT8 In[16], UINT8 Out[16]);
int IwlAesUnwrap(const UINT8 Kek[16], const UINT8 *In, UINTN InLen,
                 UINT8 *Out, UINTN OutCap, UINTN *OutLen);
void IwlAesCmac(const UINT8 Key[16], const UINT8 *Msg, UINTN Len, UINT8 Out[16]);
void IwlSha1(const UINT8 *Data, UINTN Len, UINT8 Out[20]);
void IwlKdfSha256(const UINT8 *Key, UINTN KeyLen, const char *Label,
                  const UINT8 *Ctx, UINTN CtxLen, UINT8 *Out, UINTN OutLen);
void IwlHmacSha1(const UINT8 *Key, UINTN KeyLen, const UINT8 *Data, UINTN Len, UINT8 Out[20]);
int IwlPbkdf2Sha1(const char *Pass, const UINT8 *Salt, UINTN SaltLen,
                  UINT32 Iter, UINT8 *Out, UINTN OutLen);
int IwlCcmpEncrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);
int IwlCcmpDecrypt(const UINT8 Key[16], UINT64 Pn, UINT8 *Frame, UINTN HdrLen, UINTN BodyLen);
int IwlTkipDecrypt(const UINT8 Tk[32], UINT8 *Frame, UINTN HdrLen, UINTN CryptLen);
int IwlTkipMichaelOk(const UINT8 Tk[32], const UINT8 *Frame, UINTN HdrLen,
                     const UINT8 *Payload, UINTN DataLen);

#endif /* IWL_PRIVATE_H */
