/*
 * BootTypes.h — RiscV Boot 本目录自用的基础整数类型
 *
 * 与 Arm64/BootTypes.h、Kernel/Include/Abi/BootTypes.h 保持一致。
 * Boot 单独可编：不依赖 Kernel/、不拉 OpenSBI 头。
 *
 * UINTN = UINT64：riscv64 LP64 下与指针同宽。
 */
#ifndef BOOT_TYPES_H
#define BOOT_TYPES_H

#define NULL ((void *)0)

typedef unsigned long long  UINT64;
typedef unsigned int        UINT32;
typedef unsigned short      UINT16;
typedef unsigned char       UINT8;
typedef long long           INT64;
typedef int                 INT32;
typedef short               INT16;
typedef signed char         INT8;
typedef UINT64              UINTN;

#endif
