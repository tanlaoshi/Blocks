/*
 * UsbHost/HighSpeedInterIntegratedCircuit.c — Exynos/Tensor HSI2C 最小主发
 *
 * 基址 zuma `10cb0000.hsi2c`；TCPC @0x25。tc12→CTL SW_RST；时序非 0 保留 ABL。
 */
#include "UsbHost/Private.h"

#ifndef BOARD_PIXEL8_USB_HOST_BRINGUP
#define BOARD_PIXEL8_USB_HOST_BRINGUP 0
#endif

#if BOARD_PIXEL8_USB_HOST_BRINGUP

#define HIGH_SPEED_INTER_INTEGRATED_CIRCUIT_PHYSICAL 0x10CB0000ULL

#define HSI2C_CONTROL                         0x00u
#define HSI2C_FIFO_CONTROL                    0x04u
#define HSI2C_TRAILING_CONTROL                0x08u
#define HSI2C_INTERRUPT_ENABLE                0x20u
#define HSI2C_INTERRUPT_STATUS                0x24u
#define HSI2C_FIFO_STATUS                     0x30u
#define HSI2C_TRANSMIT_DATA                   0x34u
#define HSI2C_RECEIVE_DATA                    0x38u
#define HSI2C_CONFIGURATION                   0x40u
#define HSI2C_AUTO_CONFIGURATION              0x44u
#define HSI2C_TIMEOUT                         0x48u
#define HSI2C_TRANSFER_STATUS                 0x50u
#define HSI2C_TIMING_FAST_SPEED_1             0x60u
#define HSI2C_TIMING_FAST_SPEED_2             0x64u
#define HSI2C_TIMING_FAST_SPEED_3             0x68u
#define HSI2C_TIMING_SLAVE                    0x6Cu
#define HSI2C_ADDRESS                         0x70u

#define HSI2C_FUNCTION_MODE_INTER_INTEGRATED  (1u << 0)
#define HSI2C_MASTER                          (1u << 3)
#define HSI2C_RECEIVE_CHANNEL_ON              (1u << 6)
#define HSI2C_TRANSMIT_CHANNEL_ON             (1u << 7)
#define HSI2C_SOFTWARE_RESET                  (1u << 31)

#define HSI2C_RECEIVE_FIFO_ENABLE             (1u << 0)
#define HSI2C_TRANSMIT_FIFO_ENABLE            (1u << 1)
#define HSI2C_AUTO_MODE                       (1u << 31)
#define HSI2C_READ_WRITE                      (1u << 16)
#define HSI2C_STOP_AFTER_TRANSFER             (1u << 17)
#define HSI2C_MASTER_RUN                      (1u << 31)
#define HSI2C_TIMEOUT_ENABLE                  (1u << 31)

#define HSI2C_INTERRUPT_TRANSFER_DONE         (1u << 7)
#define HSI2C_INTERRUPT_TRANSFER_ABORT        (1u << 8)
#define HSI2C_INTERRUPT_NO_DEVICE_ACKNOWLEDGE (1u << 9)
#define HSI2C_INTERRUPT_NO_DEVICE             (1u << 10)
#define HSI2C_INTERRUPT_TIMEOUT               (1u << 11)
#define HSI2C_INTERRUPT_TRANSFER_MASK                                       \
    (HSI2C_INTERRUPT_TRANSFER_DONE | HSI2C_INTERRUPT_TRANSFER_ABORT |       \
     HSI2C_INTERRUPT_NO_DEVICE_ACKNOWLEDGE | HSI2C_INTERRUPT_NO_DEVICE |    \
     HSI2C_INTERRUPT_TIMEOUT)

#define HSI2C_TRANSFER_DONE_STATUS            (1u << 0)
#define HSI2C_TRANSMIT_FIFO_FULL              (1u << 7)
#define HSI2C_RECEIVE_FIFO_EMPTY              (1u << 24)
#define HSI2C_SLAVE_ADDRESS_MASTER(Address)   (((Address) & 0x3FFu) << 10)

#define HSI2C_TIMING_FAST_SPEED_1_VALUE       0x01F0FF00u
#define HSI2C_TIMING_FAST_SPEED_2_VALUE       0x030003E0u
#define HSI2C_TIMING_FAST_SPEED_3_VALUE       0x001F0000u
#define HSI2C_TIMING_SLAVE_VALUE              0x0000001Fu

static UINT32 sDiagnosticControl;
static UINT32 sDiagnosticInterruptStatus;
static UINT32 sDiagnosticTransferStatus;

static volatile UINT32 *HighSpeedRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(HIGH_SPEED_INTER_INTEGRATED_CIRCUIT_PHYSICAL +
                                      (UINT64)Offset);
}

static void StallCycles(UINTN Count) {
    UINTN Index;
    for (Index = 0; Index < Count; Index++) {
        __asm__ volatile("yield" ::: "memory");
    }
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticControl(void) {
    return sDiagnosticControl;
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticInterrupt(void) {
    return sDiagnosticInterruptStatus;
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticTransfer(void) {
    return sDiagnosticTransferStatus;
}

static void HighSpeedControllerInitialize(void) {
    UINT32 Timeout;
    UINT32 Control;
    UINT32 TimingFastSpeed1;

    UsbHostUniversalSerialInterfaceEnableInterIntegratedCircuit();

    /* CTL SW_RST：清卡死 FSM（i2c-exynos5_reset）；非关 CMU 钟 */
    Control = *HighSpeedRegister(HSI2C_CONTROL);
    *HighSpeedRegister(HSI2C_CONTROL) = Control | HSI2C_SOFTWARE_RESET;
    Control = *HighSpeedRegister(HSI2C_CONTROL);
    *HighSpeedRegister(HSI2C_CONTROL) = Control & ~HSI2C_SOFTWARE_RESET;

    TimingFastSpeed1 = *HighSpeedRegister(HSI2C_TIMING_FAST_SPEED_1);
    if (TimingFastSpeed1 == 0u) {
        *HighSpeedRegister(HSI2C_TIMING_FAST_SPEED_1) =
            HSI2C_TIMING_FAST_SPEED_1_VALUE;
        *HighSpeedRegister(HSI2C_TIMING_FAST_SPEED_2) =
            HSI2C_TIMING_FAST_SPEED_2_VALUE;
        *HighSpeedRegister(HSI2C_TIMING_FAST_SPEED_3) =
            HSI2C_TIMING_FAST_SPEED_3_VALUE;
        *HighSpeedRegister(HSI2C_TIMING_SLAVE) = HSI2C_TIMING_SLAVE_VALUE;
    }

    Timeout = *HighSpeedRegister(HSI2C_TIMEOUT);
    Timeout &= ~HSI2C_TIMEOUT_ENABLE;
    *HighSpeedRegister(HSI2C_TIMEOUT) = Timeout;

    *HighSpeedRegister(HSI2C_CONTROL) =
        HSI2C_FUNCTION_MODE_INTER_INTEGRATED | HSI2C_MASTER;
    *HighSpeedRegister(HSI2C_TRAILING_CONTROL) = 0xFu;
    *HighSpeedRegister(HSI2C_CONFIGURATION) =
        (*HighSpeedRegister(HSI2C_CONFIGURATION)) | HSI2C_AUTO_MODE;

    sDiagnosticControl = *HighSpeedRegister(HSI2C_CONTROL);
}

static int HighSpeedWaitTransferDone(void) {
    UINTN Guard;
    UINT32 InterruptStatus;
    UINT32 TransferStatus;

    for (Guard = 0; Guard < 4000000u; Guard++) {
        InterruptStatus = *HighSpeedRegister(HSI2C_INTERRUPT_STATUS);
        TransferStatus = *HighSpeedRegister(HSI2C_TRANSFER_STATUS);
        sDiagnosticInterruptStatus = InterruptStatus;
        sDiagnosticTransferStatus = TransferStatus;

        (void)TransferStatus; /* 诊断用；autov9 完成位只认 INT_TRANS_DONE */
        if ((InterruptStatus & (HSI2C_INTERRUPT_TRANSFER_ABORT |
                                HSI2C_INTERRUPT_NO_DEVICE_ACKNOWLEDGE |
                                HSI2C_INTERRUPT_NO_DEVICE |
                                HSI2C_INTERRUPT_TIMEOUT)) != 0) {
            *HighSpeedRegister(HSI2C_INTERRUPT_STATUS) = InterruptStatus;
            return -1;
        }
        /* 勿用 TRANS_STATUS bit0：idle 常粘 1 → 假成功、读全 ff */
        if ((InterruptStatus & HSI2C_INTERRUPT_TRANSFER_DONE) != 0) {
            *HighSpeedRegister(HSI2C_INTERRUPT_STATUS) = InterruptStatus;
            return 0;
        }
        StallCycles(10u);
    }
    return -2;
}

static int HighSpeedStartWrite(UINT8 DeviceAddress, const UINT8 *Bytes,
                               UINT32 Length) {
    UINT32 Index;
    UINT32 Control;

    HighSpeedControllerInitialize();
    *HighSpeedRegister(HSI2C_INTERRUPT_STATUS) = 0xFFFFFFFFu;
    *HighSpeedRegister(HSI2C_INTERRUPT_ENABLE) = HSI2C_INTERRUPT_TRANSFER_MASK;
    *HighSpeedRegister(HSI2C_FIFO_CONTROL) =
        HSI2C_RECEIVE_FIFO_ENABLE | HSI2C_TRANSMIT_FIFO_ENABLE;
    Control = HSI2C_FUNCTION_MODE_INTER_INTEGRATED | HSI2C_MASTER |
              HSI2C_TRANSMIT_CHANNEL_ON;
    *HighSpeedRegister(HSI2C_CONTROL) = Control;
    sDiagnosticControl = Control;
    *HighSpeedRegister(HSI2C_ADDRESS) =
        HSI2C_SLAVE_ADDRESS_MASTER((UINT32)DeviceAddress);

    for (Index = 0; Index < Length; Index++) {
        UINTN Guard = 0;
        while (((*HighSpeedRegister(HSI2C_FIFO_STATUS)) &
                HSI2C_TRANSMIT_FIFO_FULL) != 0 &&
               Guard < 100000u) {
            Guard++;
        }
        *HighSpeedRegister(HSI2C_TRANSMIT_DATA) = Bytes[Index];
    }

    *HighSpeedRegister(HSI2C_AUTO_CONFIGURATION) =
        Length | HSI2C_STOP_AFTER_TRANSFER | HSI2C_MASTER_RUN;
    return HighSpeedWaitTransferDone();
}

int UsbHostHighSpeedInterIntegratedCircuitWrite(UINT8 DeviceAddress,
                                                  const UINT8 *Bytes,
                                                  UINT32 Length) {
    if (Bytes == 0 || Length == 0 || Length > 16u) {
        return -3;
    }
    return HighSpeedStartWrite(DeviceAddress, Bytes, Length);
}

int UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
    UINT8 DeviceAddress, UINT8 RegisterAddress, UINT8 *Bytes, UINT32 Length) {
    UINT32 Index;
    int Result;

    if (Bytes == 0 || Length == 0 || Length > 16u) {
        return -3;
    }

    Result = HighSpeedStartWrite(DeviceAddress, &RegisterAddress, 1u);
    if (Result != 0) {
        return Result;
    }

    HighSpeedControllerInitialize();
    *HighSpeedRegister(HSI2C_INTERRUPT_STATUS) = 0xFFFFFFFFu;
    *HighSpeedRegister(HSI2C_INTERRUPT_ENABLE) = HSI2C_INTERRUPT_TRANSFER_MASK;
    *HighSpeedRegister(HSI2C_FIFO_CONTROL) =
        HSI2C_RECEIVE_FIFO_ENABLE | HSI2C_TRANSMIT_FIFO_ENABLE;
    *HighSpeedRegister(HSI2C_CONTROL) =
        HSI2C_FUNCTION_MODE_INTER_INTEGRATED | HSI2C_MASTER |
        HSI2C_RECEIVE_CHANNEL_ON;
    *HighSpeedRegister(HSI2C_ADDRESS) =
        HSI2C_SLAVE_ADDRESS_MASTER((UINT32)DeviceAddress);
    *HighSpeedRegister(HSI2C_AUTO_CONFIGURATION) =
        Length | HSI2C_READ_WRITE | HSI2C_STOP_AFTER_TRANSFER |
        HSI2C_MASTER_RUN;

    Result = HighSpeedWaitTransferDone();
    if (Result != 0) {
        return Result;
    }

    for (Index = 0; Index < Length; Index++) {
        UINTN Guard = 0;
        while (((*HighSpeedRegister(HSI2C_FIFO_STATUS)) &
                HSI2C_RECEIVE_FIFO_EMPTY) != 0 &&
               Guard < 100000u) {
            Guard++;
        }
        Bytes[Index] = (UINT8)(*HighSpeedRegister(HSI2C_RECEIVE_DATA));
    }
    return 0;
}

#else

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticControl(void) {
    return 0;
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticInterrupt(void) {
    return 0;
}

UINT32 UsbHostHighSpeedInterIntegratedCircuitDiagnosticTransfer(void) {
    return 0;
}

int UsbHostHighSpeedInterIntegratedCircuitWrite(UINT8 DeviceAddress,
                                                  const UINT8 *Bytes,
                                                  UINT32 Length) {
    (void)DeviceAddress;
    (void)Bytes;
    (void)Length;
    return -1;
}

int UsbHostHighSpeedInterIntegratedCircuitWriteThenRead(
    UINT8 DeviceAddress, UINT8 RegisterAddress, UINT8 *Bytes, UINT32 Length) {
    (void)DeviceAddress;
    (void)RegisterAddress;
    (void)Bytes;
    (void)Length;
    return -1;
}

#endif
