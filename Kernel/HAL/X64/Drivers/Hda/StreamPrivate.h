/*
 * StreamPrivate.h — HDA Stream 内部：开机钟声
 */
#ifndef HDA_STREAM_PRIVATE_H
#define HDA_STREAM_PRIVATE_H

#include "BootTypes.h"

int HdaStreamPlayBuiltinTone(INT16 *Pcm, UINTN CapacityBytes,
                             int (*RunDmaCallback)(UINT32 PcmBytes));

#endif
