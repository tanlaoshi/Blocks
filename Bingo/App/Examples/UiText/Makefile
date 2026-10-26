# UiText — .uitxt → MyAppUi.h → MYAPP.ELF（须树内 Kernel：Layout.c + uitxt2h）
#   make -C Bingo/App/Examples/UiText

BLOCKS_KERNEL ?= $(abspath ../../../../Kernel)
PROG      ?= MYAPP
SRCS      ?= main.c UiActions.c
UITXT     ?= MyApp.uitxt
UIH       ?= MyAppUi.h

LAYOUT_C  := $(BLOCKS_KERNEL)/User/Library/Ui/Layout.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Ui/libUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include $(BLOCKS_KERNEL)/User/BlocksUser.mk

LAYOUT_O  := $(BUILDDIR)/UiLayout.o
EXTRA_OBJS = $(LAYOUT_O)

$(LAYOUT_O): $(LAYOUT_C) $(BLOCKS_KERNEL)/User/include/UiLayout.h | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $(LAYOUT_C) -o $@

$(BUILDDIR)/main.o $(BUILDDIR)/UiActions.o: $(UIH)
