/*
 * SamsungUart.c — Exynos UART 轮询（对表 S3C2410_UTRSTAT / UTXH）
 */
#include "HalSerial/SamsungUart.h"
#include "BoardConfig.h"
#include "SerialConfig.h"

#ifndef BOARD_UART_BASE
#error "BOARD_UART_BASE required for Samsung UART"
#endif

#define UART_BASE ((UINTN)BOARD_UART_BASE)

/* Linux include/linux/serial_s3c.h */
#define TRANSMIT_STATUS          (*(volatile UINT32 *)(UART_BASE + 0x10))
#define RECEIVE_STATUS_ERROR     (*(volatile UINT32 *)(UART_BASE + 0x14))
#define TRANSMIT_HOLDING         (*(volatile UINT32 *)(UART_BASE + 0x20))
#define RECEIVE_HOLDING          (*(volatile UINT32 *)(UART_BASE + 0x24))

#define TRANSMIT_STATUS_BUFFER_EMPTY (1u << 2)
#define TRANSMIT_STATUS_TRANSMITTER_EMPTY (1u << 1)
#define RECEIVE_STATUS_DATA_READY (1u << 0)

void SamsungUartInitialize(void) {
#if SERIAL_ENABLE
    /* ABL / earlycon 已开钟与波特率；此处只打心跳确认基址。 */
    SamsungUartPutByte((UINT8)'B');
    SamsungUartPutByte((UINT8)'l');
    SamsungUartPutByte((UINT8)'o');
    SamsungUartPutByte((UINT8)'c');
    SamsungUartPutByte((UINT8)'k');
    SamsungUartPutByte((UINT8)'s');
    SamsungUartPutByte((UINT8)' ');
    SamsungUartPutByte((UINT8)'u');
    SamsungUartPutByte((UINT8)'a');
    SamsungUartPutByte((UINT8)'r');
    SamsungUartPutByte((UINT8)'t');
    SamsungUartPutByte((UINT8)'\r');
    SamsungUartPutByte((UINT8)'\n');
    SamsungUartDrainTransmitter();
#endif
}

void SamsungUartPutByte(UINT8 Value) {
#if SERIAL_ENABLE
    while ((TRANSMIT_STATUS & TRANSMIT_STATUS_BUFFER_EMPTY) == 0) {
    }
    TRANSMIT_HOLDING = (UINT32)Value;
    __asm__ volatile("dsb sy" ::: "memory");
#else
    (void)Value;
#endif
}

int SamsungUartDataReady(void) {
#if SERIAL_ENABLE
    return (TRANSMIT_STATUS & RECEIVE_STATUS_DATA_READY) ? 1 : 0;
#else
    return 0;
#endif
}

char SamsungUartReadByte(void) {
#if SERIAL_ENABLE
    while (SamsungUartDataReady() == 0) {
    }
    (void)RECEIVE_STATUS_ERROR;
    return (char)(RECEIVE_HOLDING & 0xFFu);
#else
    return 0;
#endif
}

void SamsungUartDrainTransmitter(void) {
#if SERIAL_ENABLE
    UINT32 Guard = 0;

    while ((TRANSMIT_STATUS & TRANSMIT_STATUS_TRANSMITTER_EMPTY) == 0 &&
           Guard < 200000u) {
        __asm__ volatile("nop");
        Guard++;
    }
#endif
}
