ARCH ?= x86_64
# PR-B2：Arm/RiscV 板包选择 → HAL/<Arch>/Board/<board>/；x86 桌面真机走 1.3c，忽略 BOARD
BOARD ?= virt
DEBUG ?= 0
NO_COM1 ?= 0
# 串口/屏幕总开关 + 分模块（见 Include/SerialConfig.h）；NO_COM1=1 ⇒ SERIAL=0
SERIAL ?= 1
SERIAL_BOOT ?= 1
SERIAL_USB ?= 1
SERIAL_SMP ?= 1
SERIAL_GUI ?= 1
SERIAL_NET ?= 1
SERIAL_FS ?= 1
SERIAL_MEM ?= 1
SERIAL_DRV ?= 1
SERIAL_MISC ?= 1
SCREEN_LOG ?= 0
# 开机日志落盘 BOOT.LOG。无串口单机默认开。FILE_LOG=0 关闭。
FILE_LOG ?= 1
# SCREEN_LOG=1 时的分模块默认；总关时下方 ifeq 清零
SCREEN_LOG_BOOT ?= 1
SCREEN_LOG_USB ?= 1
SCREEN_LOG_SMP ?= 0
SCREEN_LOG_GUI ?= 1
SCREEN_LOG_NET ?= 0
SCREEN_LOG_FS ?= 1
SCREEN_LOG_MEM ?= 0
SCREEN_LOG_DRV ?= 0
SCREEN_LOG_MISC ?= 0
ifeq ($(NO_COM1),1)
SERIAL := 0
endif
ifeq ($(SERIAL),0)
SERIAL_BOOT := 0
SERIAL_USB := 0
SERIAL_SMP := 0
SERIAL_GUI := 0
SERIAL_NET := 0
SERIAL_FS := 0
SERIAL_MEM := 0
SERIAL_DRV := 0
SERIAL_MISC := 0
endif
ifeq ($(SCREEN_LOG),0)
SCREEN_LOG_BOOT := 0
SCREEN_LOG_USB := 0
SCREEN_LOG_SMP := 0
SCREEN_LOG_GUI := 0
SCREEN_LOG_NET := 0
SCREEN_LOG_FS := 0
SCREEN_LOG_MEM := 0
SCREEN_LOG_DRV := 0
SCREEN_LOG_MISC := 0
endif
LWIP ?= 1
LWIPINCLUDES :=
LWIPOBJS :=
LWIP_PORT_OBJS :=
BOARD_OBJS :=

# PR-A6/A7：BRINGUP=1 仅串口 hello；默认 0 链完整 Common
BRINGUP ?= 0

CC = gcc
LD = ld
OBJCOPY = objcopy

# 可选：旁挂 Blocks/Tools 或本树 Tools（xPack 交叉链，目录名 xpack-*）
TOOLS_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../Tools)
ifeq ($(wildcard $(TOOLS_ROOT)/xpack-*),)
TOOLS_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/Tools)
endif


ifeq ($(ARCH),x86_64)
HAL_ARCH = X64
# -mgeneral-regs-only：禁止内核生成 SSE/AVX（未开 CR4.OSFXSR 时 pxor %xmm 会 #UD）
ARCH_CFLAGS = -m64 -mno-red-zone -mgeneral-regs-only
USER_BLOB_FMT = elf64-x86-64
LDFLAGS_ARCH =
endif
ifeq ($(ARCH),riscv)
HAL_ARCH = RiscV
ARCH_CFLAGS = -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany
LDFLAGS_ARCH =
ifneq ($(wildcard $(TOOLS_ROOT)/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-gcc),)
TOOLS_RISCV := $(lastword $(sort $(wildcard $(TOOLS_ROOT)/xpack-riscv-none-elf-gcc-*)))
CC = $(TOOLS_RISCV)/bin/riscv-none-elf-gcc
LD = $(TOOLS_RISCV)/bin/riscv-none-elf-ld
OBJCOPY = $(TOOLS_RISCV)/bin/riscv-none-elf-objcopy
else ifneq ($(shell command -v riscv64-linux-gnu-gcc 2>/dev/null),)
CC = riscv64-linux-gnu-gcc
LD = riscv64-linux-gnu-ld
OBJCOPY = riscv64-linux-gnu-objcopy
else ifneq ($(shell command -v riscv64-unknown-elf-gcc 2>/dev/null),)
CC = riscv64-unknown-elf-gcc
LD = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy
endif
endif
ifeq ($(ARCH),arm64)
HAL_ARCH = Arm64
ARCH_CFLAGS = -mgeneral-regs-only
LDFLAGS_ARCH =
ifneq ($(wildcard $(TOOLS_ROOT)/xpack-aarch64-none-elf-gcc-*/bin/aarch64-none-elf-gcc),)
TOOLS_ARM := $(lastword $(sort $(wildcard $(TOOLS_ROOT)/xpack-aarch64-none-elf-gcc-*)))
CC = $(TOOLS_ARM)/bin/aarch64-none-elf-gcc
LD = $(TOOLS_ARM)/bin/aarch64-none-elf-ld
OBJCOPY = $(TOOLS_ARM)/bin/aarch64-none-elf-objcopy
else ifneq ($(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),)
CC = aarch64-linux-gnu-gcc
LD = aarch64-linux-gnu-ld
OBJCOPY = aarch64-linux-gnu-objcopy
else ifneq ($(shell command -v aarch64-none-elf-gcc 2>/dev/null),)
CC = aarch64-none-elf-gcc
LD = aarch64-none-elf-ld
OBJCOPY = aarch64-none-elf-objcopy
endif
endif

INCLUDES_COMMON = -IInclude \
                  -IInclude/Abi \
                  -IInclude/Hal \
                  -IInclude/Core \
                  -IInclude/Driver \
                  -IInclude/Library \
                  -IInclude/Services \
                  -ILibrary \
                  -ILibrary/Fonts \
                  -IThirdParty/stb \
                  -IHAL/$(HAL_ARCH) \
                  -IHAL/$(HAL_ARCH)/Hal \
                  -IHAL/$(HAL_ARCH)/Architecture \
                  -IHAL/$(HAL_ARCH)/PageTable \
                  -IHAL/$(HAL_ARCH)/PCIe \
                  -IHAL/$(HAL_ARCH)/IoApic \
                  -IHAL/$(HAL_ARCH)/DeviceEnum \
                  -IHAL/$(HAL_ARCH)/SmpBoot \
                  -IHAL/$(HAL_ARCH)/Platform \
                  -IHAL/$(HAL_ARCH)/Acpi \
                  -IBringUp/$(HAL_ARCH)
INCLUDES_HAL    = $(INCLUDES_COMMON) \
                  -IHAL/$(HAL_ARCH)/Drivers \
                  -IHAL/$(HAL_ARCH)/Drivers/Bus \
                  -IHAL/$(HAL_ARCH)/Drivers/Input \
                  -IHAL/$(HAL_ARCH)/Drivers/Block \
                  -IHAL/$(HAL_ARCH)/Drivers/Video/Igpu \
                  -IHAL/$(HAL_ARCH)/Drivers/Model

# PR-B2：非 x86 必须选中 Board 包（默认 virt）；Common 不 -I 板目录
ifeq ($(ARCH),x86_64)
BOARD_DIR :=
else
BOARD_DIR := HAL/$(HAL_ARCH)/Board/$(BOARD)
ifeq ($(wildcard $(BOARD_DIR)/README.md),)
$(error BOARD=$(BOARD): missing $(BOARD_DIR)/ (see HAL/Board/README.md); default BOARD=virt)
endif
INCLUDES_HAL += -I$(BOARD_DIR)
endif

XHCI_DIAG_VERBOSE ?= 0
DEMO_DRIVER ?= 1
KERNEL_TASK_DEMO ?= 0
CFLAGS_BASE = -ffreestanding -nostdlib -O2 -Wall -Wextra \
              -fno-stack-protector -fno-builtin -fno-pie -fno-pic \
              -DKERNEL_DEBUG=$(DEBUG) -DBRINGUP=$(BRINGUP) \
              -DKERNEL_TASK_DEMO=$(KERNEL_TASK_DEMO) \
              -DSERIAL_ENABLE=$(SERIAL) \
              -DSERIAL_BOOT=$(SERIAL_BOOT) \
              -DSERIAL_USB=$(SERIAL_USB) \
              -DSERIAL_SMP=$(SERIAL_SMP) \
              -DSERIAL_GUI=$(SERIAL_GUI) \
              -DSERIAL_NET=$(SERIAL_NET) \
              -DSERIAL_FS=$(SERIAL_FS) \
              -DSERIAL_MEM=$(SERIAL_MEM) \
              -DSERIAL_DRV=$(SERIAL_DRV) \
              -DSERIAL_MISC=$(SERIAL_MISC) \
              -DSCREEN_LOG=$(SCREEN_LOG) \
              -DSCREEN_LOG_BOOT=$(SCREEN_LOG_BOOT) \
              -DSCREEN_LOG_USB=$(SCREEN_LOG_USB) \
              -DSCREEN_LOG_SMP=$(SCREEN_LOG_SMP) \
              -DSCREEN_LOG_GUI=$(SCREEN_LOG_GUI) \
              -DSCREEN_LOG_NET=$(SCREEN_LOG_NET) \
              -DSCREEN_LOG_FS=$(SCREEN_LOG_FS) \
              -DSCREEN_LOG_MEM=$(SCREEN_LOG_MEM) \
              -DSCREEN_LOG_DRV=$(SCREEN_LOG_DRV) \
              -DSCREEN_LOG_MISC=$(SCREEN_LOG_MISC) \
              -DFILE_LOG=$(FILE_LOG) \
              -DXHCI_DIAG_VERBOSE=$(XHCI_DIAG_VERBOSE) \
              -DDEMO_DRIVER=$(DEMO_DRIVER) \
              -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
              $(ARCH_CFLAGS)

ifeq ($(LWIP),1)
CFLAGS_BASE += -DLWIP_ENABLE=1
LWIPDIR = ThirdParty/lwip/src
LWIPINCLUDES = -I$(LWIPDIR)/include \
               -IHAL/$(HAL_ARCH)/LwIp/include \
               -IHAL/$(HAL_ARCH)/LwIp
LWIPCORE = \
	$(LWIPDIR)/core/init.c \
	$(LWIPDIR)/core/def.c \
	$(LWIPDIR)/core/inet_chksum.c \
	$(LWIPDIR)/core/ip.c \
	$(LWIPDIR)/core/mem.c \
	$(LWIPDIR)/core/memp.c \
	$(LWIPDIR)/core/netif.c \
	$(LWIPDIR)/core/pbuf.c \
	$(LWIPDIR)/core/raw.c \
	$(LWIPDIR)/core/stats.c \
	$(LWIPDIR)/core/sys.c \
	$(LWIPDIR)/core/tcp.c \
	$(LWIPDIR)/core/tcp_in.c \
	$(LWIPDIR)/core/tcp_out.c \
	$(LWIPDIR)/core/timeouts.c \
	$(LWIPDIR)/core/udp.c \
	$(LWIPDIR)/core/dns.c \
	$(LWIPDIR)/core/ipv4/etharp.c \
	$(LWIPDIR)/core/ipv4/icmp.c \
	$(LWIPDIR)/core/ipv4/ip4.c \
	$(LWIPDIR)/core/ipv4/ip4_addr.c \
	$(LWIPDIR)/core/ipv4/dhcp.c \
	$(LWIPDIR)/netif/ethernet.c
LWIPOBJS = $(patsubst %.c,$(BUILDDIR)/%.o,$(LWIPCORE))
LWIP_PORT_SRCS = HAL/$(HAL_ARCH)/LwIp/Netif.c \
                 HAL/$(HAL_ARCH)/LwIp/Ping.c \
                 HAL/$(HAL_ARCH)/LwIp/TcpEcho.c \
                 HAL/$(HAL_ARCH)/LwIp/UdpQueue.c \
                 HAL/$(HAL_ARCH)/LwIp/TcpClient.c \
                 HAL/$(HAL_ARCH)/LwIp/TcpSocket.c
LWIP_PORT_OBJS = $(patsubst HAL/$(HAL_ARCH)/LwIp/%.c,$(HALDIR)/LwIp/%.o,$(LWIP_PORT_SRCS))
endif

CFLAGS_COMMON = $(CFLAGS_BASE) $(INCLUDES_COMMON) $(LWIPINCLUDES)
CFLAGS_HAL    = $(CFLAGS_BASE) $(INCLUDES_HAL) $(LWIPINCLUDES) -IHAL/$(HAL_ARCH)/LwIp
# PR-UI-ttf-fpu：仅 HalFpuSse.c 去掉 general-regs-only，允许 SSE
CFLAGS_FPU    = $(filter-out -mgeneral-regs-only,$(CFLAGS_HAL)) -msse2
ifneq ($(BOARD_DIR),)
endif

LDFLAGS = -nostdlib -static -z noexecstack -T HAL/$(HAL_ARCH)/link.ld -e KernelEntry $(LDFLAGS_ARCH)
# SpinLock 的 __sync_* 需要 libgcc（如 __aarch64_swp4_sync）
LIBGCC := $(shell $(CC) $(ARCH_CFLAGS) -print-libgcc-file-name 2>/dev/null)

# 产物树：Common、Services、Library、Core、User、Fonts 与 HAL 同级；HAL 下按 Arch 分目录。
# 有 ToyOS 树时由 build.sh 传入 BUILDDIR=$TOYOS_ROOT/Build/ToyKernel；单仓默认 ./Build。
# 各 Arch 的 Kernel.elf / HAL .o 留在 $(BUILDDIR)/HAL/<Arch>/，换架构不删其它 Arch 成品。
BUILDDIR ?= Build
HALDIR = $(BUILDDIR)/HAL/$(HAL_ARCH)

# Common / Services / Library / Core / User / Fonts 的 .o 跨 Arch 共用路径：换 ARCH 时若仍用旧 .o 会链错格式
# （如 EM:62 x86_64 → aarch64/riscv）。勿只清 Common。
ARCH_STAMP := $(BUILDDIR)/.toy_arch
_STAMP_ARCH := $(shell cat $(ARCH_STAMP) 2>/dev/null)
ifneq ($(_STAMP_ARCH),$(ARCH))
$(info ARCH: stale '$(_STAMP_ARCH)' → '$(ARCH)'; cleaning $(BUILDDIR)/{CodeA–E,lwip})
$(shell rm -rf '$(BUILDDIR)/HAL' '$(BUILDDIR)/Library' \
	'$(BUILDDIR)/Core' '$(BUILDDIR)/BringUp' '$(BUILDDIR)/Services' '$(BUILDDIR)/User' \
	'$(BUILDDIR)/ThirdParty/lwip' '$(BUILDDIR)/lwip')
endif
$(shell mkdir -p '$(BUILDDIR)' && echo '$(ARCH)' > '$(ARCH_STAMP)')

# PR-B3：同 HALDIR 多板包共用；BOARD 与 .toy_board 不一致（或缺 stamp）时清 HAL，避免链错 UART/Config
ifneq ($(ARCH),x86_64)
BOARD_STAMP := $(HALDIR)/.toy_board
ifneq ($(wildcard $(HALDIR)/Kernel.elf),)
_STAMP_BOARD := $(shell cat $(BOARD_STAMP) 2>/dev/null)
ifneq ($(_STAMP_BOARD),$(BOARD))
$(info BOARD: stale build '$(_STAMP_BOARD)' → '$(BOARD)'; cleaning $(HALDIR))
$(shell rm -rf '$(HALDIR)')
endif
endif
endif

# PR-D-tpl-2：DEMO_DRIVER 变了必须重编 HalDevices（裸 make 换宏否则会「无需做任何事」）
DEMO_STAMP := $(HALDIR)/.toy_demo_driver
.PHONY: FORCE
FORCE:
$(DEMO_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(DEMO_DRIVER)' > $@.new
	@if [ ! -f $@ ] || ! cmp -s $@.new $@; then mv $@.new $@; else rm -f $@.new; fi

# BringUp：启动段（Entry/Handoff 分 arch + KernelMain/Modules 合流）；非契约圆
BRINGUP_SRCS  := $(wildcard BringUp/*.c)
BRINGUP_OBJS  := $(patsubst BringUp/%.c,$(BUILDDIR)/BringUp/%.o,$(BRINGUP_SRCS))
BRINGUP_ARCH_SRCS := $(wildcard BringUp/$(HAL_ARCH)/*.c)
BRINGUP_ARCH_ASM  := $(wildcard BringUp/$(HAL_ARCH)/*.S)
BRINGUP_ARCH_OBJS := $(patsubst BringUp/$(HAL_ARCH)/%.c,$(BUILDDIR)/BringUp/$(HAL_ARCH)/%.o,$(BRINGUP_ARCH_SRCS)) \
                     $(patsubst BringUp/$(HAL_ARCH)/%.S,$(BUILDDIR)/BringUp/$(HAL_ARCH)/%.o,$(BRINGUP_ARCH_ASM))
# Core：内核机制；薄 Initialize* 在 HAL/Initialize 与 Services（见下）
CORE_SRCS     := $(wildcard Core/*.c)
CORE_SRCS     += $(wildcard Core/Driver/*.c)
CORE_SRCS     += $(wildcard Core/FileSystem/*.c)
CORE_SRCS     += $(filter-out Core/Scheduler/RoundRobin.c,$(wildcard Core/Scheduler/*.c))
CORE_SRCS     += $(wildcard Core/Process/*.c)
CORE_SRCS     += $(wildcard Core/TaskFd/*.c)
CORE_SRCS     += $(wildcard Core/Syscall/*.c)
CORE_SRCS     += $(wildcard Core/VirtualMemory/*.c)
CORE_SRCS     += $(filter-out Core/PhysicalMemory/Bitmap.c,$(wildcard Core/PhysicalMemory/*.c))
CORE_SRCS     += $(wildcard Core/Elf/*.c)
CORE_SRCS     += $(wildcard Core/Device/*.c)
# 开机 Init（Hal 侧，架构无关胶水；用 CFLAGS_COMMON）
HAL_INIT_SRCS := $(wildcard HAL/Initialize/*.c)
HAL_INIT_OBJS := $(patsubst HAL/Initialize/%.c,$(BUILDDIR)/HAL/Initialize/%.o,$(HAL_INIT_SRCS))
SERVICES_SRCS := $(wildcard Services/*.c)
# Services/*.c 不进子目录；每个模块开目录时补一行
SERVICES_SRCS += $(wildcard Services/Look/Locale/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Drag/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Draw/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Pointer/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Resize/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/User/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Open/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Backup/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Focus/*.c)
SERVICES_SRCS += $(wildcard Services/Shell/Console/*.c)
SERVICES_SRCS += $(wildcard Services/Desktop/*.c)
SERVICES_SRCS += $(wildcard Services/Apps/FilesUi/*.c)
SERVICES_SRCS += $(wildcard Services/Store/Catalog/*.c)
SERVICES_SRCS += $(wildcard Services/Apps/SettingsUi/*.c)
SERVICES_SRCS += $(wildcard Services/Look/Theme/*.c)
SERVICES_SRCS += $(wildcard Services/Store/Ui/*.c)
SERVICES_SRCS += $(wildcard Services/Apps/DevicesUi/*.c)
SERVICES_SRCS += $(wildcard Services/Apps/EditUi/*.c)
SERVICES_SRCS += $(wildcard Services/Apps/TerminalUi/*.c)
SERVICES_SRCS += $(wildcard Services/Store/Net/*.c)
SERVICES_SRCS += $(wildcard Services/Database/*.c)
SERVICES_SRCS += $(wildcard Services/FileSystem/*.c)
SERVICES_SRCS += $(wildcard Services/Tasks/*.c)
SERVICES_SRCS += $(wildcard Services/Shell/Commands/*.c)
SERVICES_SRCS += $(wildcard Services/Net/Tcp/*.c)
SERVICES_SRCS += $(wildcard Services/Net/Network/*.c)
SERVICES_SRCS += $(wildcard Services/Net/LwIp/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Compose/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/Cursor/*.c)
SERVICES_SRCS += $(wildcard Services/Gui/WindowManager/*.c)
SERVICES_SRCS += $(wildcard Services/Ui/*.c)
SERVICES_SRCS += $(wildcard Services/Look/Fonts/*.c)
FS ?= fat
LIB_SRCS      := $(wildcard Library/*.c)
LIB_SRCS      += $(wildcard Library/Block/*.c)
LIB_SRCS      += $(wildcard Library/VirtualFileSystem/*.c)
LIB_SRCS      += $(wildcard Library/Gpt/*.c)
LIB_SRCS      += $(wildcard Library/Graphics/*.c)
LIB_SRCS      += $(wildcard Library/Bmp/*.c)
ifeq ($(FS),fat)
LIB_SRCS      += $(wildcard Library/Fat/*.c)
endif
FONT_SRCS     := $(wildcard Library/Fonts/*.c)
DRIVER_SRCS   := $(wildcard HAL/$(HAL_ARCH)/Drivers/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Video/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Network/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/VirtioNet/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/E1000/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Alx/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Rtl8168/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Rtl8188eu/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Iwl/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Iwl/*/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Model/Iwl/*/*/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/Ehci/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/Ehci/Wifi/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/Uhci/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Input/Ps2/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Block/Nvme/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Block/Ahci/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Block/Ata/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Msc/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/Hub/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Input/UsbHid/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Serial/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Video/Igpu/*.c)
DRIVER_SRCS   += $(wildcard HAL/$(HAL_ARCH)/Drivers/Hda/*.c)
# PR-H-xhci-split-8：Drivers/XHCI/*.c（Core/Port/Device/Hid/Hub/Mouse/Irq/Diag）；已删单体 Drivers/XHCI.c
XHCI_SPLIT_SRCS := $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/*.c)
XHCI_SPLIT_SRCS += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/Msc/*.c)
XHCI_SPLIT_SRCS += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/Hid/*.c)
XHCI_SPLIT_SRCS += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/Serial/Ftdi/*.c)
XHCI_SPLIT_SRCS += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/Serial/Cdc/*.c)
XHCI_SPLIT_SRCS += $(wildcard HAL/$(HAL_ARCH)/Drivers/Bus/XHCI/Wifi/*.c)
DRIVER_SRCS   += $(XHCI_SPLIT_SRCS)
ARCH_SRCS     := $(wildcard HAL/$(HAL_ARCH)/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Architecture/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/PageTable/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/PCIe/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/IoApic/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/DeviceEnum/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/SmpBoot/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Platform/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Hal/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)
ARCH_SRCS     += $(wildcard HAL/$(HAL_ARCH)/Acpi/*.c)
ARCH_ASM_ALL  := $(wildcard HAL/$(HAL_ARCH)/*.S)
ARCH_ASM_ALL  += $(wildcard HAL/$(HAL_ARCH)/Architecture/*.S)
ARCH_ASM_ALL  += $(wildcard HAL/$(HAL_ARCH)/SmpBoot/*.S)
ARCH_ASM      := $(filter-out HAL/$(HAL_ARCH)/SmpBoot/Trampoline.S,$(ARCH_ASM_ALL))

CORE_OBJS     := $(patsubst Core/%.c,$(BUILDDIR)/Core/%.o,$(CORE_SRCS))
SERVICES_OBJS := $(patsubst Services/%.c,$(BUILDDIR)/Services/%.o,$(SERVICES_SRCS))
LIB_OBJS      := $(patsubst Library/%.c,$(BUILDDIR)/Library/%.o,$(LIB_SRCS))
FONT_OBJS     := $(patsubst Library/Fonts/%.c,$(BUILDDIR)/Library/Fonts/%.o,$(FONT_SRCS))
DRIVER_OBJS   := $(patsubst HAL/$(HAL_ARCH)/Drivers/%.c,$(HALDIR)/Drivers/%.o,$(DRIVER_SRCS))
ARCH_OBJS     := $(patsubst HAL/$(HAL_ARCH)/%.c,$(HALDIR)/%.o,$(ARCH_SRCS))
ARCH_ASM_OBJS := $(patsubst HAL/$(HAL_ARCH)/%.S,$(HALDIR)/%.o,$(ARCH_ASM))

ifeq ($(ARCH),x86_64)
EXTRA_OBJS = $(HALDIR)/User_hello_blob.o $(HALDIR)/SmpTramp_blob.o
# 演示 ELF/OBJ 进 Build/User/（源在 User/Apps/）；CRT/Library 仍就地编
USER_OUT = $(BUILDDIR)/User
USER_HELLO_ELF = $(USER_OUT)/hello.elf
USER_COUNT_ELF = $(USER_OUT)/count.elf
USER_FORK_ELF = $(USER_OUT)/fork.elf
USER_WAITNH_ELF = $(USER_OUT)/waitnh.elf
USER_DYNDEMO_ELF = $(USER_OUT)/dyndemo.elf
USER_LIBTOY_SO = $(USER_OUT)/libtoy.so
USER_CAT_ELF = $(USER_OUT)/catfile.elf
USER_WRITE_ELF = $(USER_OUT)/writefile.elf
USER_NETDEMO_ELF = $(USER_OUT)/netdemo.elf
USER_NETSRV_ELF = $(USER_OUT)/netsrv.elf
USER_SYSHELLO_ELF = $(USER_OUT)/syshello.elf
USER_SYSFORK_ELF = $(USER_OUT)/sysfork.elf
USER_EXECDEMO_ELF = $(USER_OUT)/execdemo.elf
USER_PIPEDEMO_ELF = $(USER_OUT)/pipedemo.elf
USER_BRKDEMO_ELF = $(USER_OUT)/brkdemo.elf
USER_MMAPDEMO_ELF = $(USER_OUT)/mmapdemo.elf
USER_KILLDEMO_ELF = $(USER_OUT)/killdemo.elf
USER_SIGDEMO_ELF = $(USER_OUT)/sigdemo.elf
USER_WINDEMO_ELF = $(USER_OUT)/windemo.elf
USER_GUIDEMO_ELF = $(USER_OUT)/guidemo.elf
USER_BLITDEMO_ELF = $(USER_OUT)/blitdemo.elf
USER_LIBCDEMO_ELF = $(USER_OUT)/libcdemo.elf
USER_SLEEPDEMO_ELF = $(USER_OUT)/sleepdemo.elf
USER_THREADSMOKE_ELF = $(USER_OUT)/threadsmoke.elf
USER_PTHREADSMOKE_ELF = $(USER_OUT)/pthreadsmoke.elf
USER_THREADDEMO_ELF = $(USER_OUT)/threaddemo.elf
USER_SNAKE_ELF = $(USER_OUT)/snake.elf
USER_TASKMGR_ELF = $(USER_OUT)/taskmgr.elf
USER_DIRDEMO_ELF = $(USER_OUT)/dirdemo.elf
USER_CWDDEMO_ELF = $(USER_OUT)/cwddemo.elf
USER_NETLIB_ELF = $(USER_OUT)/netlibdemo.elf
USER_SOCKDEMO_ELF = $(USER_OUT)/sockdemo.elf
USER_CHAT_ELF = $(USER_OUT)/chat.elf
USER_ENOSYS_ELF = $(USER_OUT)/enosysdemo.elf
USER_HELLO_OBJ = $(USER_OUT)/hello.o
USER_COUNT_OBJ = $(USER_OUT)/count.o
USER_FORK_OBJ = $(USER_OUT)/fork.o
USER_WAITNH_OBJ = $(USER_OUT)/waitnh.o
USER_DYNDEMO_OBJ = $(USER_OUT)/dyndemo.o
USER_LIBTOY_OBJ = $(USER_OUT)/libtoy.o
USER_CAT_OBJ = $(USER_OUT)/cat.o
USER_WRITE_OBJ = $(USER_OUT)/writefile.o
USER_NETDEMO_OBJ = $(USER_OUT)/netdemo.o
USER_NETSRV_OBJ = $(USER_OUT)/netsrv.o
USER_SYSHELLO_OBJ = $(USER_OUT)/syshello.o
USER_SYSFORK_OBJ = $(USER_OUT)/sysfork.o
USER_EXECDEMO_OBJ = $(USER_OUT)/execdemo.o
USER_PIPEDEMO_OBJ = $(USER_OUT)/pipedemo.o
USER_BRKDEMO_OBJ = $(USER_OUT)/brkdemo.o
USER_MMAPDEMO_OBJ = $(USER_OUT)/mmapdemo.o
USER_KILLDEMO_OBJ = $(USER_OUT)/killdemo.o
USER_SIGDEMO_OBJ = $(USER_OUT)/sigdemo.o
USER_WINDEMO_OBJ = $(USER_OUT)/windemo.o
USER_GUIDEMO_OBJ = $(USER_OUT)/guidemo.o
USER_BLITDEMO_OBJ = $(USER_OUT)/blitdemo.o
USER_LIBCDEMO_OBJ = $(USER_OUT)/libcdemo.o
USER_SLEEPDEMO_OBJ = $(USER_OUT)/sleepdemo.o
USER_THREADSMOKE_OBJ = $(USER_OUT)/threadsmoke.o
USER_PTHREADSMOKE_OBJ = $(USER_OUT)/pthreadsmoke.o
USER_THREADDEMO_OBJ = $(USER_OUT)/threaddemo.o
USER_SNAKE_OBJ = $(USER_OUT)/snake.o
USER_TASKMGR_OBJ = $(USER_OUT)/taskmgr.o
USER_DIRDEMO_OBJ = $(USER_OUT)/dirdemo.o
USER_CWDDEMO_OBJ = $(USER_OUT)/cwddemo.o
USER_NETLIB_OBJ = $(USER_OUT)/netlibdemo.o
USER_SOCKDEMO_OBJ = $(USER_OUT)/sockdemo.o
USER_CHAT_OBJ = $(USER_OUT)/chat.o
USER_ENOSYS_OBJ = $(USER_OUT)/enosysdemo.o
USER_LIB_GRAPHICS_OBJ = User/Library/Graphics/Graphics.o
USER_LIB_GRAPHICS_BATCH_OBJ = User/Library/Graphics/Batch.o
USER_LIB_UI_OBJ = User/Library/Ui/Ui.o
USER_LIB_UI_WIDGETS_OBJ = User/Library/Ui/Widgets.o
USER_LIB_UI_IM_OBJ = User/Library/Ui/Immediate.o
USER_LIB_UI_STYLE_OBJ = User/Library/Ui/Style.o
USER_LIB_TOY_NET_OBJ = User/Library/ToyNet/ToyNet.o
USER_LIB_FSUTIL_OBJ = User/Library/VolumePath/VolumePath.o
USER_LIB_GRAPHICS_A = User/Library/Graphics/libGraphics.a
USER_LIB_UI_A = User/Library/Ui/libUi.a
USER_LIB_TOY_NET_A = User/Library/ToyNet/libToyNet.a
USER_LIB_FSUTIL_A = User/Library/VolumePath/libVolumePath.a
USER_LIB_BLOCKS_A = User/Library/Blocks/libBlocks.a
USER_LIB_BLOCKS_OBJS = User/crt/string.o User/crt/printf.o User/crt/malloc.o \
	User/crt/errno.o User/crt/unistd.o User/crt/sleep.o User/crt/stdlib.o User/crt/signal.o \
	User/crt/dirent.o User/crt/stdio.o User/crt/socket.o User/crt/cwd.o User/crt/sched.o \
	User/crt/proc.o User/crt/stat.o User/crt/threadroot.o User/crt/pthread.o
USER_LD = User/user.ld
USER_LDFLAGS = -z noexecstack
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic -m64 -mno-red-zone -IUser/include -IInclude -IInclude/Abi
USER_ASFLAGS = -IUser/include -IInclude -IInclude/Abi
USER_CRT_OBJS = User/crt/crt0.o User/crt/syscall.o $(USER_LIB_BLOCKS_OBJS)
else
EXTRA_OBJS =
# PR-B2：Board 包 .c（BoardConfig.h 仅 HAL -I；不进 Common）
BOARD_SRCS := $(wildcard $(BOARD_DIR)/*.c)
BOARD_OBJS := $(patsubst $(BOARD_DIR)/%.c,$(HALDIR)/Board/%.o,$(BOARD_SRCS))
EXTRA_OBJS += $(BOARD_OBJS)
# PR-R5：Arm/RiscV 共享 virtio/ramfb/DTB/HalVideo（HAL/Virtio）；复用 x86 Video 绘制
INCLUDES_HAL += -IHAL/X64/Drivers -IHAL/X64/Drivers/Input -IHAL/X64/Drivers/Block -IHAL/X64/Drivers/Video/Igpu -IHAL
VIRT_SRCS := $(wildcard HAL/Virtio/*.c)
VIRT_SRCS += $(wildcard HAL/Virtio/Net/*.c)
VIRT_OBJS := $(patsubst HAL/Virtio/%.c,$(HALDIR)/Virtio/%.o,$(VIRT_SRCS))
VIRT_VIDEO_SRCS := $(wildcard HAL/X64/Drivers/Video/*.c)
VIRT_VIDEO_OBJS := $(patsubst HAL/X64/Drivers/Video/%.c,$(HALDIR)/Drivers/Video/%.o,$(VIRT_VIDEO_SRCS))
EXTRA_OBJS += $(VIRT_OBJS) $(VIRT_VIDEO_OBJS)
EXTRA_OBJS += $(HALDIR)/Drivers/Network/IpInput.o
ifeq ($(ARCH),riscv)
# RiscV virt MMIO 窗与 Arm 不同；Arm 用 Virtio.c 内默认值
CFLAGS_HAL += -DVIRTIO_MMIO_BASE0=0x10001000ULL \
              -DVIRTIO_MMIO_STRIDE=0x1000u \
              -DVIRTIO_MMIO_COUNT=8u
endif
# PR-A12：本 arch 静态 HELLO.ELF（用户 VA @ 0x100000000）；放在 Arch 目录以免换架构互相覆盖
USER_VIRT_DIR = $(HALDIR)/user
ifeq ($(ARCH),arm64)
USER_LD = User/user-arm64.ld
USER_CRT0_SRC = User/crt/crt0_aarch64.S
USER_SYSCALL_SRC = User/crt/syscall_aarch64.S
USER_LDFLAGS = -z noexecstack
else
USER_LD = User/user-riscv.ld
USER_CRT0_SRC = User/crt/crt0_riscv.S
USER_SYSCALL_SRC = User/crt/syscall_riscv.S
USER_LDFLAGS = -m elf64lriscv -z noexecstack
endif
USER_CFLAGS = -ffreestanding -nostdlib -O2 -Wall -Wextra -fno-stack-protector \
	-fno-builtin -fno-pie -fno-pic $(ARCH_CFLAGS) -IUser/include -IInclude -IInclude/Abi
ifeq ($(ARCH),arm64)
# pthread __sync_*：内联原子，避免裸链依赖 __aarch64_swp4_sync（libgcc）
USER_CFLAGS += -mno-outline-atomics
endif
USER_HELLO_ELF = $(USER_VIRT_DIR)/hello.elf
USER_HELLO_OBJ = $(USER_VIRT_DIR)/hello.o
USER_CRT_OBJS = $(USER_VIRT_DIR)/crt0.o $(USER_VIRT_DIR)/syscall.o \
	$(USER_VIRT_DIR)/string.o $(USER_VIRT_DIR)/printf.o \
	$(USER_VIRT_DIR)/malloc.o $(USER_VIRT_DIR)/errno.o \
	$(USER_VIRT_DIR)/unistd.o $(USER_VIRT_DIR)/sleep.o $(USER_VIRT_DIR)/stdlib.o \
	$(USER_VIRT_DIR)/signal.o $(USER_VIRT_DIR)/dirent.o \
	$(USER_VIRT_DIR)/stdio.o $(USER_VIRT_DIR)/socket.o \
	$(USER_VIRT_DIR)/cwd.o $(USER_VIRT_DIR)/sched.o $(USER_VIRT_DIR)/proc.o \
	$(USER_VIRT_DIR)/stat.o $(USER_VIRT_DIR)/threadroot.o $(USER_VIRT_DIR)/pthread.o
endif

SCHEDULER ?= round-robin
ifeq ($(SCHEDULER),round-robin)
SCHED_SRCS := Core/Scheduler/RoundRobin.c
else ifeq ($(SCHEDULER),priority)
SCHED_SRCS := Core/Scheduler/Student/Priority.c
CFLAGS_COMMON += -DSCHED_PRIORITY
else
$(error unknown SCHEDULER=$(SCHEDULER))
endif
SCHED_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(SCHED_SRCS))

MEMORY ?= bitmap
ifeq ($(MEMORY),bitmap)
MEMORY_SRCS := Core/PhysicalMemory/Bitmap.c
else ifeq ($(MEMORY),bestfit)
MEMORY_SRCS := Core/PhysicalMemory/Student/BestFit.c
CFLAGS_COMMON += -DMEM_BESTFIT
else
$(error Unknown MEMORY: $(MEMORY))
endif
MEMORY_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(MEMORY_SRCS))

ifeq ($(FS),fat)
FS_SRCS :=
else ifeq ($(FS),ram)
FS_SRCS := Core/FileSystem/Student/FileSystemRam.c \
           Core/FileSystem/Student/FileSystemRamCompat.c
CFLAGS_COMMON += -DFS_RAM
else
$(error Unknown FS: $(FS))
endif
FS_OBJS := $(patsubst %.c,$(BUILDDIR)/%.o,$(FS_SRCS))

OBJS = $(BRINGUP_ARCH_OBJS) $(BRINGUP_OBJS) $(CORE_OBJS) $(SERVICES_OBJS) $(LIB_OBJS) $(FONT_OBJS) $(DRIVER_OBJS) $(ARCH_OBJS) $(ARCH_ASM_OBJS) $(EXTRA_OBJS) $(HAL_INIT_OBJS) $(SCHED_OBJS) $(MEMORY_OBJS) $(FS_OBJS) $(LWIPOBJS) $(LWIP_PORT_OBJS)
TARGET = $(HALDIR)/Kernel.elf

ifeq ($(BRINGUP),1)
# PR-A6：BringUp Entry/Handoff* + HalSerial + Hal（Halt），不链 Common
# PR-B2：仍链 Board.o（横幅 BoardName；HalSerial 用 BoardConfig）
# X64 的 HandoffConfig/Memory/Platform 与 KernelHandoff 同在 BRINGUP_ARCH_OBJS
OBJS = $(BRINGUP_ARCH_OBJS) \
       $(patsubst HAL/$(HAL_ARCH)/Hal/HalSerial/%.c,$(HALDIR)/Hal/HalSerial/%.o,$(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial/*.c)) \
       $(patsubst HAL/$(HAL_ARCH)/Hal/%.c,$(HALDIR)/Hal/%.o,$(wildcard HAL/$(HAL_ARCH)/Hal/HalSerial.c)) \
       $(HALDIR)/Hal/Hal.o \
       $(BOARD_OBJS)
EXTRA_OBJS =
endif

# 汇编用同一 BRINGUP（KernelEntry → KernelHandoff）
ASFLAGS_ARCH = -DBRINGUP=$(BRINGUP)

.PHONY: all clean boards kernel-bin runtests runtests-memory runtests-fs scheduler check-library-pure check-syscall check-elf
.DEFAULT_GOAL := all

all: check-library-pure check-syscall check-elf

HOSTCC ?= gcc
TEST_CFLAGS = -std=c11 -Wall -Wextra -DSCHED_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(SCHEDULER),priority)
TEST_CFLAGS += -DSCHED_PRIORITY
endif

MEM_TEST_CFLAGS = -std=c11 -Wall -Wextra -DMEM_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(MEMORY),bestfit)
MEM_TEST_CFLAGS += -DMEM_BESTFIT
endif

FS_TEST_CFLAGS = -std=c11 -Wall -Wextra -DFS_HOST -I Tools/Tests/Stub -I Include -I Include/Abi -I Include/Core -I Include/Driver -I Include/Library -I Include/Services -I Include/Hal
ifeq ($(FS),ram)
FS_TEST_CFLAGS += -DFS_RAM
FS_TEST_POLICY := Core/FileSystem/Student/FileSystemRam.c
else
FS_TEST_POLICY := Tools/Tests/Stub/FsStub.c
endif

scheduler: runtests

runtests:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(TEST_CFLAGS) -c $(SCHED_SRCS) -o $(BUILDDIR)/Tests/policy.o
	$(HOSTCC) $(TEST_CFLAGS) -c Core/Scheduler/Operations.c -o $(BUILDDIR)/Tests/ops.o
	$(HOSTCC) $(TEST_CFLAGS) -c Tools/Tests/Stub/SchedulerStub.c -o $(BUILDDIR)/Tests/stub.o
	$(HOSTCC) $(TEST_CFLAGS) -c Tools/Tests/TestScheduler.c -o $(BUILDDIR)/Tests/test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestScheduler $(BUILDDIR)/Tests/policy.o $(BUILDDIR)/Tests/ops.o $(BUILDDIR)/Tests/stub.o $(BUILDDIR)/Tests/test.o
	./$(BUILDDIR)/Tests/TestScheduler

runtests-memory:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c $(MEMORY_SRCS) -o $(BUILDDIR)/Tests/mem_policy.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c Core/PhysicalMemory/Operations.c -o $(BUILDDIR)/Tests/mem_ops.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c Tools/Tests/Stub/MemoryStub.c -o $(BUILDDIR)/Tests/mem_stub.o
	$(HOSTCC) $(MEM_TEST_CFLAGS) -c Tools/Tests/TestMemory.c -o $(BUILDDIR)/Tests/mem_test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestMemory $(BUILDDIR)/Tests/mem_policy.o $(BUILDDIR)/Tests/mem_ops.o $(BUILDDIR)/Tests/mem_stub.o $(BUILDDIR)/Tests/mem_test.o
	./$(BUILDDIR)/Tests/TestMemory

runtests-fs:
	@mkdir -p $(BUILDDIR)/Tests
	$(HOSTCC) $(FS_TEST_CFLAGS) -c Library/VirtualFileSystem.c -o $(BUILDDIR)/Tests/fs_vfs.o
	$(HOSTCC) $(FS_TEST_CFLAGS) -c $(FS_TEST_POLICY) -o $(BUILDDIR)/Tests/fs_policy.o
	$(HOSTCC) $(FS_TEST_CFLAGS) -c Tools/Tests/TestFs.c -o $(BUILDDIR)/Tests/fs_test.o
	$(HOSTCC) -o $(BUILDDIR)/Tests/TestFs $(BUILDDIR)/Tests/fs_vfs.o $(BUILDDIR)/Tests/fs_policy.o $(BUILDDIR)/Tests/fs_test.o
	./$(BUILDDIR)/Tests/TestFs

all: $(TARGET)
ifneq ($(ARCH),x86_64)
	@echo "Built BOARD=$(BOARD) ($(BOARD_DIR))"
endif

# PR-B3：扁平二进制（U-Boot load / go；非 Linux Image 头）
kernel-bin: $(TARGET)
ifeq ($(ARCH),x86_64)
	$(error kernel-bin: x86 uses ELF via ToyBoot; not applicable)
endif
	$(OBJCOPY) -O binary $(TARGET) $(HALDIR)/Kernel.bin
	@echo "Wrote $(HALDIR)/Kernel.bin (BOARD=$(BOARD))"
ifeq ($(ARCH),x86_64)
ifneq ($(BRINGUP),1)
all: $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF) \
	$(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF) \
	$(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF) \
	$(USER_EXECDEMO_ELF) $(USER_PIPEDEMO_ELF) $(USER_BRKDEMO_ELF) $(USER_MMAPDEMO_ELF) $(USER_KILLDEMO_ELF) \
	$(USER_SIGDEMO_ELF) \
	$(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_SLEEPDEMO_ELF) $(USER_THREADSMOKE_ELF) $(USER_PTHREADSMOKE_ELF) $(USER_THREADDEMO_ELF) $(USER_SNAKE_ELF) $(USER_TASKMGR_ELF) $(USER_DIRDEMO_ELF) $(USER_CWDDEMO_ELF) \
	$(USER_NETLIB_ELF) $(USER_SOCKDEMO_ELF) $(USER_CHAT_ELF) $(USER_ENOSYS_ELF) $(USER_LIB_BLOCKS_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A)
endif
else
ifneq ($(BRINGUP),1)
all: $(USER_HELLO_ELF)
endif
endif

# PR-B2：列出当前 Arch 可用板包
boards:
ifeq ($(ARCH),x86_64)
	@echo "BOARD: (unused on x86_64; desktop PC is 1.3c / HAL/X64)"
else
	@echo "ARCH=$(ARCH) HAL_ARCH=$(HAL_ARCH) BOARD=$(BOARD) → $(BOARD_DIR)"
	@ls -1 HAL/$(HAL_ARCH)/Board 2>/dev/null | sed 's/^/  /' || echo "  (none)"
endif

$(BUILDDIR) $(HALDIR):
	mkdir -p $@

$(TARGET): $(OBJS) | $(HALDIR)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)
ifneq ($(ARCH),x86_64)
	@echo "$(BOARD)" > $(HALDIR)/.toy_board
endif

$(BUILDDIR)/BringUp/%.o: BringUp/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/BringUp/$(HAL_ARCH)/%.o: BringUp/$(HAL_ARCH)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(BUILDDIR)/BringUp/$(HAL_ARCH)/%.o: BringUp/$(HAL_ARCH)/%.S | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) $(ASFLAGS_ARCH) -c $< -o $@

$(BUILDDIR)/Core/%.o: Core/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/HAL/Initialize/%.o: HAL/Initialize/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

# 须先于通用 Services/%.o，保证 CFLAGS_FPU
$(BUILDDIR)/Services/Look/Fonts/TrueTypeRaster.o: Services/Look/Fonts/TrueTypeRaster.c | $(BUILDDIR)
ifeq ($(ARCH),x86_64)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_FPU) -Wno-unused-parameter -c $< -o $@
else
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@
endif

$(BUILDDIR)/Services/%.o: Services/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Library/%.o: Library/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

$(BUILDDIR)/Library/Fonts/%.o: Library/Fonts/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_COMMON) -c $< -o $@

# Library 去 Hal 门禁（纯化后勿再引入）
check-library-pure:
	@if rg -n '#include[[:space:]]+"(Hal|PhysicalMemory|VirtualMemory|FileSystem|Device|Debug|ToySerialLog|Scheduler|Process|Console|Theme|Gui)\.h"' Library/ --glob '*.{c,h}' ; then \
		echo 'error: Library must not depend on Hal/Core/Services modules' >&2; exit 1; \
	fi
	@if rg -n 'Hal[A-Z][A-Za-z]+\(' Library/ --glob '*.{c,h}' ; then \
		echo 'error: Library must not call Hal*' >&2; exit 1; \
	fi
	@echo 'Library pure OK'

check-syscall:
	@python3 Tools/CheckSyscall.py

check-elf:
	@python3 Tools/CheckElf.py

$(HALDIR)/Drivers/%.o: HAL/$(HAL_ARCH)/Drivers/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

# 先于通用 HAL/%.o：依赖 DEMO_STAMP，换 DEMO_DRIVER=0/1 会触发重编+重链
$(HALDIR)/Hal/HalDevices.o: HAL/$(HAL_ARCH)/Hal/HalDevices.c $(DEMO_STAMP) | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifneq ($(ARCH),x86_64)
$(HALDIR)/Drivers/Video/%.o: HAL/X64/Drivers/Video/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/Virtio/%.o: HAL/Virtio/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/Drivers/Network/IpInput.o: HAL/X64/Drivers/Network/IpInput.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

# PR-B2：HAL/<Arch>/Board/<board>/*.c
$(HALDIR)/Board/%.o: $(BOARD_DIR)/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@
endif

ifeq ($(ARCH),x86_64)
$(HALDIR)/Hal/HalFpuSse.o: HAL/X64/Hal/HalFpuSse.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_FPU) -c $< -o $@
endif

$(HALDIR)/%.o: HAL/$(HAL_ARCH)/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

$(HALDIR)/LwIp/%.o: HAL/$(HAL_ARCH)/LwIp/%.c | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(LWIP),1)
$(BUILDDIR)/ThirdParty/lwip/src/%.o: $(LWIPDIR)/%.c | $(BUILDDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@
endif

$(HALDIR)/%.o: HAL/$(HAL_ARCH)/%.S | $(HALDIR)
	@mkdir -p $(dir $@) && $(CC) $(CFLAGS_HAL) -c $< -o $@

ifeq ($(ARCH),x86_64)
$(HALDIR)/SmpTramp.bin: HAL/X64/SmpBoot/Trampoline.S HAL/X64/SmpBoot/Trampoline.ld | $(HALDIR)
	$(CC) -c HAL/X64/SmpBoot/Trampoline.S -o $(HALDIR)/SmpTrampoline_low.o
	$(LD) -z noexecstack -T HAL/X64/SmpBoot/Trampoline.ld -o $(HALDIR)/SmpTrampoline_low.elf \
		$(HALDIR)/SmpTrampoline_low.o
	objcopy -O binary $(HALDIR)/SmpTrampoline_low.elf $@

$(HALDIR)/SmpTramp_blob.o: $(HALDIR)/SmpTramp.bin
	cd $(HALDIR) && objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
		SmpTramp.bin SmpTramp_blob.o

$(USER_HELLO_OBJ): User/Apps/Hello.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/stddef.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Hello.c -o $@

User/crt/%.o: User/crt/%.c
	$(CC) $(USER_CFLAGS) -c $< -o $@

User/crt/%.o: User/crt/%.S
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_COUNT_OBJ): User/Apps/Assembly/Count.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS)

$(USER_EXECDEMO_OBJ): User/Apps/Demos/ExecDemo.c User/include/stdio.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/ExecDemo.c -o $@

$(USER_EXECDEMO_ELF): $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_EXECDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_PIPEDEMO_OBJ): User/Apps/Demos/PipeDemo.c User/include/stdio.h User/include/unistd.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/PipeDemo.c -o $@

$(USER_PIPEDEMO_ELF): $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_PIPEDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_BRKDEMO_OBJ): User/Apps/Demos/BrkDemo.c User/include/stdio.h User/include/stdlib.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/BrkDemo.c -o $@

$(USER_BRKDEMO_ELF): $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BRKDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_MMAPDEMO_OBJ): User/Apps/Demos/MmapDemo.c User/include/stdio.h User/include/string.h User/include/sys/mman.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/MmapDemo.c -o $@

$(USER_MMAPDEMO_ELF): $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_MMAPDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_KILLDEMO_OBJ): User/Apps/Demos/KillDemo.c User/include/stdio.h User/include/unistd.h User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/KillDemo.c -o $@

$(USER_KILLDEMO_ELF): $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_KILLDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SIGDEMO_OBJ): User/Apps/Demos/SignalDemo.c User/include/stdio.h User/include/stdlib.h User/include/unistd.h User/include/signal.h User/include/Blocks/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/SignalDemo.c -o $@

$(USER_SIGDEMO_ELF): $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SIGDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_WINDEMO_OBJ): User/Apps/Demos/WindowDemo.c User/include/stdio.h User/include/unistd.h User/include/Blocks/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/WindowDemo.c -o $@

$(USER_WINDEMO_ELF): $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WINDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_LIB_GRAPHICS_OBJ): User/Library/Graphics/Graphics.c User/include/Graphics.h User/include/unistd.h User/include/Blocks/syscall.h 
	$(CC) $(USER_CFLAGS) -c User/Library/Graphics/Graphics.c -o $@

$(USER_LIB_GRAPHICS_BATCH_OBJ): User/Library/Graphics/Batch.c \
		User/include/GraphicsBatch.h User/include/Graphics.h
	$(CC) $(USER_CFLAGS) -c User/Library/Graphics/Batch.c -o $@

$(USER_LIB_UI_OBJ): User/Library/Ui/Ui.c User/Library/Ui/Private.h \
		User/include/Ui.h User/include/Graphics.h User/include/unistd.h \
		User/include/Blocks/syscall.h
	$(CC) $(USER_CFLAGS) -c User/Library/Ui/Ui.c -o $@

$(USER_LIB_UI_WIDGETS_OBJ): User/Library/Ui/Widgets.c \
		User/Library/Ui/Private.h User/include/Ui.h User/include/Graphics.h
	$(CC) $(USER_CFLAGS) -c User/Library/Ui/Widgets.c -o $@

$(USER_LIB_UI_IM_OBJ): User/Library/Ui/Immediate.c \
		User/include/UiImmediate.h User/include/Ui.h
	$(CC) $(USER_CFLAGS) -c User/Library/Ui/Immediate.c -o $@

$(USER_LIB_UI_STYLE_OBJ): User/Library/Ui/Style.c \
		User/include/UiStyle.h
	$(CC) $(USER_CFLAGS) -c User/Library/Ui/Style.c -o $@

$(USER_LIB_GRAPHICS_A): $(USER_LIB_GRAPHICS_OBJ) $(USER_LIB_GRAPHICS_BATCH_OBJ)
	ar rcs $@ $(USER_LIB_GRAPHICS_OBJ) $(USER_LIB_GRAPHICS_BATCH_OBJ)

$(USER_LIB_UI_A): $(USER_LIB_UI_OBJ) $(USER_LIB_UI_WIDGETS_OBJ) $(USER_LIB_UI_IM_OBJ) $(USER_LIB_UI_STYLE_OBJ)
	ar rcs $@ $(USER_LIB_UI_OBJ) $(USER_LIB_UI_WIDGETS_OBJ) $(USER_LIB_UI_IM_OBJ) $(USER_LIB_UI_STYLE_OBJ)

$(USER_LIB_TOY_NET_OBJ): User/Library/ToyNet/ToyNet.c User/include/ToyNet.h \
		User/include/unistd.h User/include/errno.h User/include/string.h \
		User/include/Blocks/syscall.h
	$(CC) $(USER_CFLAGS) -c User/Library/ToyNet/ToyNet.c -o $@

$(USER_LIB_TOY_NET_A): $(USER_LIB_TOY_NET_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_TOY_NET_OBJ)

$(USER_LIB_FSUTIL_OBJ): User/Library/VolumePath/VolumePath.c User/include/VolumePath.h User/include/dirent.h User/include/errno.h 
	$(CC) $(USER_CFLAGS) -c User/Library/VolumePath/VolumePath.c -o $@

$(USER_LIB_FSUTIL_A): $(USER_LIB_FSUTIL_OBJ)
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_FSUTIL_OBJ)

# PR-L1：CRT C 部分打成 libBlocks.a，供 User/Package 课外链接
$(USER_LIB_BLOCKS_A): $(USER_LIB_BLOCKS_OBJS)
	@mkdir -p User/Library/Blocks
	mkdir -p $(dir $@)
	ar rcs $@ $(USER_LIB_BLOCKS_OBJS)

$(USER_GUIDEMO_OBJ): User/Apps/Demos/GuiDemo.c User/include/Ui.h User/include/Graphics.h User/include/stdio.h User/include/unistd.h User/include/Blocks/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/GuiDemo.c -o $@

$(USER_GUIDEMO_ELF): $(USER_GUIDEMO_OBJ) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_GUIDEMO_OBJ) \
		$(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS)

$(USER_BLITDEMO_OBJ): User/Apps/Demos/BlitDemo.c User/include/Ui.h User/include/Graphics.h User/include/stdio.h User/include/unistd.h User/include/Blocks/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/BlitDemo.c -o $@

$(USER_BLITDEMO_ELF): $(USER_BLITDEMO_OBJ) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_BLITDEMO_OBJ) \
		$(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS)

$(USER_LIBCDEMO_OBJ): User/Apps/Demos/LibcDemo.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/LibcDemo.c -o $@

$(USER_LIBCDEMO_ELF): $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_LIBCDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SLEEPDEMO_OBJ): User/Apps/Demos/SleepDemo.c User/include/stdio.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/SleepDemo.c -o $@

$(USER_SLEEPDEMO_ELF): $(USER_SLEEPDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SLEEPDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_THREADSMOKE_OBJ): User/Apps/Demos/ThreadSmoke.c User/include/stdio.h User/include/unistd.h User/include/Blocks/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/ThreadSmoke.c -o $@
$(USER_THREADSMOKE_ELF): $(USER_THREADSMOKE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_THREADSMOKE_OBJ) $(USER_CRT_OBJS)

$(USER_PTHREADSMOKE_OBJ): User/Apps/Demos/PthreadSmoke.c User/include/pthread.h User/include/stdio.h User/include/unistd.h User/include/Blocks/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/PthreadSmoke.c -o $@
$(USER_PTHREADSMOKE_ELF): $(USER_PTHREADSMOKE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_PTHREADSMOKE_OBJ) $(USER_CRT_OBJS)

$(USER_THREADDEMO_OBJ): User/Apps/Demos/ThreadDemo.c User/include/pthread.h User/include/stdio.h User/include/stdlib.h User/include/unistd.h User/include/sched.h User/include/Blocks/thread.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/ThreadDemo.c -o $@
$(USER_THREADDEMO_ELF): $(USER_THREADDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_THREADDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_SNAKE_OBJ): User/Apps/Snake.c User/include/Ui.h User/include/Graphics.h User/include/stdio.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Snake.c -o $@

$(USER_SNAKE_ELF): $(USER_SNAKE_OBJ) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SNAKE_OBJ) \
		$(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS)

$(USER_TASKMGR_OBJ): User/Apps/TaskMgr.c User/include/Ui.h User/include/Blocks/task.h User/include/stdio.h User/include/unistd.h User/include/signal.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/TaskMgr.c -o $@

$(USER_TASKMGR_ELF): $(USER_TASKMGR_OBJ) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_TASKMGR_OBJ) \
		$(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS)

$(USER_DIRDEMO_OBJ): User/Apps/Demos/DirDemo.c User/include/stdio.h User/include/dirent.h User/include/string.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/DirDemo.c -o $@

$(USER_DIRDEMO_ELF): $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_DIRDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_CWDDEMO_OBJ): User/Apps/Demos/WorkingDirectoryDemo.c User/include/stdio.h User/include/stdlib.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/WorkingDirectoryDemo.c -o $@

$(USER_CWDDEMO_ELF): $(USER_CWDDEMO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CWDDEMO_OBJ) $(USER_CRT_OBJS)

$(USER_NETLIB_OBJ): User/Apps/Demos/NetLibDemo.c User/include/ToyNet.h User/include/stdio.h User/include/string.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/NetLibDemo.c -o $@

$(USER_NETLIB_ELF): $(USER_NETLIB_OBJ) $(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETLIB_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS)

$(USER_SOCKDEMO_OBJ): User/Apps/Demos/SocketDemo.c User/include/sys/socket.h User/include/ToyNet.h User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/SocketDemo.c -o $@

$(USER_SOCKDEMO_ELF): $(USER_SOCKDEMO_OBJ) $(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SOCKDEMO_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_CRT_OBJS)

$(USER_CHAT_OBJ): User/Apps/Chat/Chat.c User/Apps/Chat/Net.h User/include/ToyNet.h User/include/Ui.h User/include/Graphics.h User/include/stdio.h User/include/unistd.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Chat/Chat.c -o $@

USER_CHAT_NET_OBJ = $(USER_OUT)/chat_net.o
$(USER_CHAT_NET_OBJ): User/Apps/Chat/Net.c User/Apps/Chat/Net.h User/include/ToyNet.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Chat/Net.c -o $@

$(USER_CHAT_ELF): $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) $(USER_LIB_TOY_NET_A) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) \
		$(USER_LIB_TOY_NET_A) $(USER_LIB_UI_A) $(USER_LIB_GRAPHICS_A) $(USER_CRT_OBJS)

$(USER_ENOSYS_OBJ): User/Apps/Demos/EnosysDemo.c User/include/stdio.h User/include/errno.h User/include/Blocks/syscall.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Demos/EnosysDemo.c -o $@

$(USER_ENOSYS_ELF): $(USER_ENOSYS_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_ENOSYS_OBJ) $(USER_CRT_OBJS)

$(USER_COUNT_ELF): $(USER_COUNT_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_COUNT_OBJ)

$(USER_FORK_OBJ): User/Apps/Assembly/Fork.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_WAITNH_OBJ): User/Apps/Assembly/WaitNoHang.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_LIBTOY_OBJ): User/Apps/Assembly/LibToy.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -fPIC -c $< -o $@

$(USER_DYNDEMO_OBJ): User/Apps/Assembly/DynamicDemo.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_CAT_OBJ): User/Apps/Cat.c User/include/unistd.h User/include/fcntl.h User/include/errno.h User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/Cat.c -o $@

$(USER_WRITE_OBJ): User/Apps/WriteFile.c User/include/unistd.h User/include/fcntl.h User/include/errno.h User/include/stdio.h | $(USER_OUT)
	$(CC) $(USER_CFLAGS) -c User/Apps/WriteFile.c -o $@

$(USER_NETDEMO_OBJ): User/Apps/Assembly/NetDemo.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_NETSRV_OBJ): User/Apps/Assembly/NetServer.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_SYSHELLO_OBJ): User/Apps/Assembly/SyscallHello.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_SYSFORK_OBJ): User/Apps/Assembly/SyscallFork.S | $(USER_OUT)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(USER_FORK_ELF): $(USER_FORK_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_FORK_OBJ)

$(USER_WAITNH_ELF): $(USER_WAITNH_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WAITNH_OBJ)

$(USER_LIBTOY_SO): $(USER_LIBTOY_OBJ) | $(USER_OUT)
	$(LD) -shared -z noexecstack -soname LIBTOY.SO -o $@ $(USER_LIBTOY_OBJ)

$(USER_DYNDEMO_ELF): $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO) | $(USER_OUT)
	$(LD) -nostdlib -no-pie -z noexecstack -Ttext-segment=0x40000000 -z max-page-size=0x1000 \
		-o $@ $(USER_DYNDEMO_OBJ) $(USER_LIBTOY_SO)

$(USER_CAT_ELF): $(USER_CAT_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_CAT_OBJ) $(USER_CRT_OBJS)

$(USER_WRITE_ELF): $(USER_WRITE_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_WRITE_OBJ) $(USER_CRT_OBJS)

$(USER_NETDEMO_ELF): $(USER_NETDEMO_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETDEMO_OBJ)

$(USER_NETSRV_ELF): $(USER_NETSRV_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_NETSRV_OBJ)

$(USER_SYSHELLO_ELF): $(USER_SYSHELLO_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SYSHELLO_OBJ)

$(USER_SYSFORK_ELF): $(USER_SYSFORK_OBJ) $(USER_LD) | $(USER_OUT)
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_SYSFORK_OBJ)

$(USER_OUT):
	mkdir -p $@

$(HALDIR)/User_hello_blob.o: $(USER_HELLO_ELF) | $(HALDIR)
	# 固定输入名为 User_hello.elf，保留 `_binary_User_hello_elf_*`（Process.c / HelloBlob.c）
	cp -f $(USER_HELLO_ELF) $(HALDIR)/User_hello.elf
	cd $(HALDIR) && objcopy -I binary -O $(USER_BLOB_FMT) User_hello.elf User_hello_blob.o
endif

ifneq ($(ARCH),x86_64)
ifneq ($(BRINGUP),1)
$(USER_VIRT_DIR):
	mkdir -p $(USER_VIRT_DIR)

$(USER_HELLO_OBJ): User/Apps/Hello.c User/include/stdio.h User/include/stdlib.h User/include/string.h User/include/stddef.h | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/Apps/Hello.c -o $@

$(USER_VIRT_DIR)/crt0.o: $(USER_CRT0_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_CRT0_SRC) -o $@

$(USER_VIRT_DIR)/syscall.o: $(USER_SYSCALL_SRC) | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c $(USER_SYSCALL_SRC) -o $@

$(USER_VIRT_DIR)/string.o: User/crt/string.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/string.c -o $@

$(USER_VIRT_DIR)/printf.o: User/crt/printf.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/printf.c -o $@

$(USER_VIRT_DIR)/malloc.o: User/crt/malloc.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/malloc.c -o $@

$(USER_VIRT_DIR)/errno.o: User/crt/errno.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/errno.c -o $@

$(USER_VIRT_DIR)/unistd.o: User/crt/unistd.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/unistd.c -o $@

$(USER_VIRT_DIR)/sleep.o: User/crt/sleep.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/sleep.c -o $@

$(USER_VIRT_DIR)/stdlib.o: User/crt/stdlib.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/stdlib.c -o $@

$(USER_VIRT_DIR)/signal.o: User/crt/signal.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/signal.c -o $@

$(USER_VIRT_DIR)/dirent.o: User/crt/dirent.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/dirent.c -o $@

$(USER_VIRT_DIR)/stdio.o: User/crt/stdio.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/stdio.c -o $@

$(USER_VIRT_DIR)/socket.o: User/crt/socket.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/socket.c -o $@

$(USER_VIRT_DIR)/cwd.o: User/crt/cwd.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/cwd.c -o $@

$(USER_VIRT_DIR)/sched.o: User/crt/sched.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/sched.c -o $@

$(USER_VIRT_DIR)/proc.o: User/crt/proc.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/proc.c -o $@

$(USER_VIRT_DIR)/stat.o: User/crt/stat.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/stat.c -o $@

$(USER_VIRT_DIR)/threadroot.o: User/crt/threadroot.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/threadroot.c -o $@

$(USER_VIRT_DIR)/pthread.o: User/crt/pthread.c | $(USER_VIRT_DIR)
	$(CC) $(USER_CFLAGS) -c User/crt/pthread.c -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(USER_LD) | $(USER_VIRT_DIR)
	# 与 Kernel.elf 同：arm64 __sync_* 可能仍需 libgcc（与 -mno-outline-atomics 双保险）
	$(LD) -nostdlib -static $(USER_LDFLAGS) -T $(USER_LD) -o $@ $(USER_HELLO_OBJ) $(USER_CRT_OBJS) $(LIBGCC)
endif
endif

clean:
	# 只清当前 Arch 的 HAL 产物；共享 Code* 产物 .o 必须清（随 ARCH 重编）
	rm -rf $(HALDIR)
	rm -rf $(BUILDDIR)/HAL $(BUILDDIR)/Library $(BUILDDIR)/Core $(BUILDDIR)/Services $(BUILDDIR)/User $(BUILDDIR)/ThirdParty/lwip $(BUILDDIR)/lwip
	# 旧布局残留
	rm -rf Build/arm64 Build/riscv
	rm -f Build/Kernel.elf Build/SmpTramp.bin Build/SmpTramp_blob.o \
		Build/SmpTrampoline_low.elf Build/SmpTrampoline_low.o Build/User_hello_blob.o
ifeq ($(ARCH),x86_64)
	rm -rf $(USER_OUT)
	rm -f $(USER_HELLO_OBJ) $(USER_COUNT_OBJ) $(USER_FORK_OBJ) $(USER_WAITNH_OBJ)
	rm -f $(USER_LIBTOY_OBJ) $(USER_DYNDEMO_OBJ) $(USER_CAT_OBJ) $(USER_WRITE_OBJ)
	rm -f $(USER_NETDEMO_OBJ) $(USER_NETSRV_OBJ) $(USER_SYSHELLO_OBJ) $(USER_SYSFORK_OBJ)
	rm -f $(USER_EXECDEMO_OBJ) $(USER_PIPEDEMO_OBJ) $(USER_BRKDEMO_OBJ) $(USER_MMAPDEMO_OBJ) $(USER_KILLDEMO_OBJ) $(USER_SIGDEMO_OBJ)
	rm -f $(USER_WINDEMO_OBJ) $(USER_GUIDEMO_OBJ) $(USER_BLITDEMO_OBJ) $(USER_LIBCDEMO_OBJ) $(USER_SLEEPDEMO_OBJ) $(USER_THREADSMOKE_OBJ) $(USER_PTHREADSMOKE_OBJ) $(USER_THREADDEMO_OBJ) $(USER_SNAKE_OBJ) $(USER_DIRDEMO_OBJ) $(USER_CWDDEMO_OBJ)
	rm -f $(USER_NETLIB_OBJ) $(USER_SOCKDEMO_OBJ) $(USER_CHAT_OBJ) $(USER_CHAT_NET_OBJ) $(USER_ENOSYS_OBJ)
	rm -f $(USER_LIB_GRAPHICS_OBJ) $(USER_LIB_GRAPHICS_BATCH_OBJ) $(USER_LIB_UI_OBJ) $(USER_LIB_UI_WIDGETS_OBJ) \
		$(USER_LIB_UI_IM_OBJ) $(USER_LIB_UI_STYLE_OBJ) \
		$(USER_LIB_TOY_NET_OBJ) $(USER_LIB_FSUTIL_OBJ)
	rm -f $(USER_LIB_GRAPHICS_A) $(USER_LIB_UI_A) $(USER_LIB_TOY_NET_A) $(USER_LIB_FSUTIL_A) $(USER_LIB_BLOCKS_A)
	rm -f $(USER_CRT_OBJS)
	rm -f $(USER_HELLO_ELF) $(USER_COUNT_ELF) $(USER_FORK_ELF) $(USER_WAITNH_ELF)
	rm -f $(USER_LIBTOY_SO) $(USER_DYNDEMO_ELF) $(USER_CAT_ELF) $(USER_WRITE_ELF)
	rm -f $(USER_NETDEMO_ELF) $(USER_NETSRV_ELF) $(USER_SYSHELLO_ELF) $(USER_SYSFORK_ELF)
	rm -f $(USER_EXECDEMO_ELF) $(USER_PIPEDEMO_ELF) $(USER_BRKDEMO_ELF) $(USER_MMAPDEMO_ELF) $(USER_KILLDEMO_ELF) $(USER_SIGDEMO_ELF)
	rm -f $(USER_WINDEMO_ELF) $(USER_GUIDEMO_ELF) $(USER_BLITDEMO_ELF) $(USER_LIBCDEMO_ELF) $(USER_SLEEPDEMO_ELF) $(USER_THREADSMOKE_ELF) $(USER_PTHREADSMOKE_ELF) $(USER_THREADDEMO_ELF) $(USER_SNAKE_ELF) $(USER_DIRDEMO_ELF) $(USER_CWDDEMO_ELF)
	rm -f $(USER_NETLIB_ELF) $(USER_SOCKDEMO_ELF) $(USER_CHAT_ELF) $(USER_ENOSYS_ELF)
endif
