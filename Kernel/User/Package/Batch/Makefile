# Batch — ToyGfxBatch 范例（PR-GFX-batch-0）
#   make -C User/Package/Batch
#   exec BATCH.ELF

TOYKERNEL ?= $(abspath ../../..)
PROG      ?= BATCH
SRCS      ?= main.c

EXTRA_LIBS = $(TOYKERNEL)/User/Library/ToyGfx/libToyGfx.a \
             $(TOYKERNEL)/User/Library/ToyUi/libToyUi.a

include ../ToyUser.mk
