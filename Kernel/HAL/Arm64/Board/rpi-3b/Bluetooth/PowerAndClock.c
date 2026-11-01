/*
 * PowerAndClock.c — Pi 3B 蓝牙的电与钟（bt-pi-1 方向 A/B）
 *
 * 依据：上游 dts 的 uart0 pinctrl 组是
 *   `uart0_gpio32 + gpclk2_gpio43`  ⇒ 除 UART 两脚外，GPIO43 还要输出 GPCLK2。
 * 仓内对表：Wifi/PinConfigure.c 里同一套（GPIO43 Alt0 + CM_GP2 32.768 kHz），
 * 这里复制一份最小实现，不改 WiFi（Docs/24 禁止碰 Wifi 目录）。
 *
 * 一次一变量（Cursor 00:50 出主意）：
 *   A：先只做 GPCLK2（本文件 PiBluetoothClock2Enable32k）
 *   B：之后才做 BT_ON 扩展脚 128（PiBluetoothPowerEnable，默认不在 A 里调用）
 */
#include "BluetoothPrivate.h"
#include "Board.h"
#include "Hal.h"
#include "HalSerial.h"

#define PI_CLOCK_MANAGER_BASE        0x3F101000ULL
#define PI_CLOCK_MANAGER_PASSWORD    0x5A000000u
#define PI_CLOCK_MANAGER_GP2_CTL     0x80u
#define PI_CLOCK_MANAGER_GP2_DIV     0x84u
#define PI_CLOCK_MANAGER_ENABLE      0x10u
#define PI_CLOCK_MANAGER_BUSY        0x80u
#define PI_CLOCK_MANAGER_SRC_OSC     1u   /* 19.2 MHz 晶振 */

#define PI_GPIO_GPPUD                0x94u
#define PI_GPIO_GPPUDCLK1            0x9Cu  /* pin 43 ≥ 32 */

static volatile UINT32 *PiBtGpioRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_GPIO_BASE + (UINT64)Offset);
}

static volatile UINT32 *PiBtClockRegister(UINT32 Offset) {
    return (volatile UINT32 *)(UINTN)(PI_CLOCK_MANAGER_BASE + (UINT64)Offset);
}

static void PiBtGpioPullOff(UINT32 Pin) {
    volatile UINT32 *Clock = PiBtGpioRegister(PI_GPIO_GPPUDCLK1);
    UINT32 Spin;

    *PiBtGpioRegister(PI_GPIO_GPPUD) = 0u; /* 0 = off */
    for (Spin = 0u; Spin < 150u; Spin++) {
        __asm__ volatile("nop");
    }
    *Clock = 1u << (Pin - 32u);
    for (Spin = 0u; Spin < 150u; Spin++) {
        __asm__ volatile("nop");
    }
    *PiBtGpioRegister(PI_GPIO_GPPUD) = 0u;
    *Clock = 0u;
}

/* GPIO43：Alt0 = GPCLK2 */
static void PiBtClockPinConfigure(void) {
    volatile UINT32 *Select = PiBtGpioRegister(PI_GPIO_GPFSEL4);
    UINT32 Shift = (43u - 40u) * 3u;
    UINT32 Value = *Select;

    Value &= ~(PI_GPIO_FSEL_MASK << Shift);
    Value |= (4u & PI_GPIO_FSEL_MASK) << Shift; /* Alt0 */
    *Select = Value;
    PiBtGpioPullOff(43u);
}

/*
 * 32.768 kHz：19.2e6/32768 = 585 + 3840/4096；MASH=1。
 * 已 ENAB|BUSY 则不动（保留固件/启动器留下的钟）。
 * 返回 0=keep，1=32k，-1=fail（供串口与诊断用）。
 */
int PiBluetoothClock2Enable32k(void) {
    volatile UINT32 *Control = PiBtClockRegister(PI_CLOCK_MANAGER_GP2_CTL);
    volatile UINT32 *Divider = PiBtClockRegister(PI_CLOCK_MANAGER_GP2_DIV);
    UINT32 Status;
    UINT32 Attempt;
    const UINT32 MashOne = (1u << 9);
    const UINT32 DividerValue = (585u << 12) | 3840u;

    PiBtClockPinConfigure();
    Status = *Control;
    if ((Status & (PI_CLOCK_MANAGER_ENABLE | PI_CLOCK_MANAGER_BUSY)) ==
        (PI_CLOCK_MANAGER_ENABLE | PI_CLOCK_MANAGER_BUSY)) {
        HalSerialBootMarkForce("Boot: Bluetooth GPCLK2 keep\n");
        return 0;
    }
    *Control = PI_CLOCK_MANAGER_PASSWORD | (Status & ~PI_CLOCK_MANAGER_ENABLE);
    for (Attempt = 0u; Attempt < 1000u; Attempt++) {
        if ((*Control & PI_CLOCK_MANAGER_BUSY) == 0u) {
            break;
        }
    }
    *Divider = PI_CLOCK_MANAGER_PASSWORD | DividerValue;
    *Control = PI_CLOCK_MANAGER_PASSWORD | PI_CLOCK_MANAGER_ENABLE | MashOne |
               PI_CLOCK_MANAGER_SRC_OSC;
    for (Attempt = 0u; Attempt < 1000u; Attempt++) {
        if ((*Control & PI_CLOCK_MANAGER_BUSY) != 0u) {
            break;
        }
    }
    Status = *Control;
    if ((Status & PI_CLOCK_MANAGER_BUSY) != 0u) {
        HalSerialBootMarkForce("Boot: Bluetooth GPCLK2 32k\n");
        return 1;
    }
    HalSerialBootMarkForce("Boot: Bluetooth GPCLK2 fail (no BUSY)\n");
    return -1;
}

/*
 * 方向 B：BT_ON = 扩展 GPIO 128（对表 dts shutdown-gpios = <&expgpio 0>）。
 * 脉冲下电再上电，对表 WifiWirelessPowerOn；延时短于 WiFi（UART ROM 不需数秒 SDIO）。
 */
int PiBluetoothPowerEnable(void) {
    (void)BoardExpanderGpioWrite(BOARD_EXPANDER_BLUETOOTH_ON, 0u);
    HalDelayMilliseconds(100u);
    if (BoardExpanderGpioWrite(BOARD_EXPANDER_BLUETOOTH_ON, 1u) != 0) {
        HalSerialBootMarkForce("Boot: Bluetooth BT_ON mbox fail\n");
        return -1;
    }
    HalDelayMilliseconds(200u);
    HalSerialBootMarkForce("Boot: Bluetooth BT_ON=1\n");
    return 0;
}
