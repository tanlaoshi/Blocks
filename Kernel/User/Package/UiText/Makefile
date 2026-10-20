# UiText — .uitxt → MyAppUi.h → MYAPP.ELF
#   make -C User/Package/UiText

BLOCKS_KERNEL ?= $(abspath ../../..)
PROG      ?= MYAPP
SRCS      ?= main.c UiActions.c
UITXT     ?= MyApp.uitxt
UIH       ?= MyAppUi.h
BUILDDIR  ?= build

LAYOUT_C  := $(BLOCKS_KERNEL)/User/Library/ToyUi/Layout.c
LAYOUT_O  := $(BUILDDIR)/ToyUiLayout.o

EXTRA_LIBS = $(BLOCKS_KERNEL)/User/Library/ToyUi/libToyUi.a \
             $(BLOCKS_KERNEL)/User/Library/Graphics/libGraphics.a
EXTRA_OBJS = $(LAYOUT_O)

include ../BlocksUser.mk

$(LAYOUT_O): $(LAYOUT_C) $(BLOCKS_KERNEL)/User/include/ToyUiLayout.h | $(BUILDDIR)
	$(CC) $(USER_CFLAGS) -c $(LAYOUT_C) -o $@

$(BUILDDIR)/main.o $(BUILDDIR)/UiActions.o: $(UIH)
