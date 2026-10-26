# UiDesigner — 设计器导出的 .uitxt → MYAPP.ELF
#   make -C User/Package/UiDesigner

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= MYAPP
SRCS      ?= main.c UiActions.c
UITXT     ?= MyApp.uitxt
UIH       ?= MyAppUi.h

LAYOUT_C  := $(BLOCKS_KERNEL)/User/Library/Ui/Layout.c

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/Ui/libUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a

include ../BlocksUser.mk

LAYOUT_O  := $(BUILDDIR)/UiLayout.o
EXTRA_OBJS = $(LAYOUT_O)

$(LAYOUT_O): $(LAYOUT_C) $(BLOCKS_KERNEL)/User/include/UiLayout.h | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $(LAYOUT_C) -o $@

$(BUILDDIR)/main.o $(BUILDDIR)/UiActions.o: $(UIH)
