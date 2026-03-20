CROSS ?= xtensa-esp32s3-elf-
ARCH_FLAGS := -mlongcalls
ARCH_SRCS_C := \
	$(ARCH_DIR)/trap.c
ARCH_SRCS_S := \
	$(ARCH_DIR)/start.S \
	$(ARCH_DIR)/context_switch.S
