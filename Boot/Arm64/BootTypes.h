/*
 * BootTypes.h — Arm64 Boot 本目录自用的基础整数类型
 *
 * 为何单独放一份？
 *   Boot 必须能单独编（不依赖 Kernel/）。这里只提供 UINT* / INT* / NULL，
 *   足够描述 BOOT_INFO，不引入 UEFI 头、也不拉 Hal。
 *
 * 注意：
 *   - 宽度按「LP64」习惯：UINTN = UINT64（与 aarch64 指针同宽）。
 *   - 与 RiscV/BootTypes.h、Kernel/Include/Abi/BootTypes.h 应对齐；改一处记三处。
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
typedef UINT64              UINTN; /* 本架构指针宽度的无符号整数 */

#endif
