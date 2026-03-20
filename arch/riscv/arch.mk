CROSS ?= riscv32-esp-elf-
ARCH_FLAGS := -march=rv32imc_zicsr -mabi=ilp32
ARCH_SRCS_C := \
	$(ARCH_DIR)/trap.c
ARCH_SRCS_S := \
	$(ARCH_DIR)/start.S \
	$(ARCH_DIR)/context_switch.S
