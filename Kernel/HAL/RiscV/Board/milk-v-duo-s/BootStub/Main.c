/*
 * Main.c — Duo BootStub：SD0 → BLOCKS Kernel.elf → 跳真核
 */
#include "BlocksVersion.h"
#include "BootTypes.h"

extern void BootStubSerialInitialize(void);
extern void BootStubSerialWrite(const char *Text);
extern int BootStubSecureDigitalInitialize(void);
extern int BootStubReadKernelExecutable(void **OutImage, UINTN *OutSize);
extern int BootStubLoadExecutableAndJump(const void *Image, UINTN ImageSize, UINT64 HartId,
                                         UINT64 DeviceTreePhys);

void BootStubMain(UINT64 HartId, UINT64 DeviceTreePhys) {
    void *Image;
    UINTN Size;

    BootStubSerialInitialize();
    BootStubSerialWrite("BootStub: Blocks V ");
    BootStubSerialWrite(BLOCKS_VERSION_STRING);
    BootStubSerialWrite("\n");

    if (BootStubSecureDigitalInitialize() != 0) {
        BootStubSerialWrite("BootStub: SD0 init fail\n");
        for (;;) {
        }
    }
    if (BootStubReadKernelExecutable(&Image, &Size) != 0) {
        BootStubSerialWrite("BootStub: load Kernel.elf fail\n");
        for (;;) {
        }
    }
    BootStubSerialWrite("BootStub: jump Kernel\n");
    (void)BootStubLoadExecutableAndJump(Image, Size, HartId, DeviceTreePhys);
    BootStubSerialWrite("BootStub: jump returned\n");
    for (;;) {
    }
}
