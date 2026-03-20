TARGET_NAME := esp32c3
TARGET_PRETTY_NAME := ESP32-C3
TARGET_ARCH := riscv
ESPTOOL_CHIP := esp32c3
IDF_TARGET := esp32c3
TARGET_INCLUDE_DIR := $(TARGET_DIR)/include
LINKER_SCRIPT := $(TARGET_DIR)/linker.ld
BOOTLOADER_PROJECT_DIR := $(TARGET_DIR)/packaging/bootloader_project
PARTITIONS_CSV := $(TARGET_DIR)/packaging/partitions.csv
BOOTLOADER_BIN := $(BOOTLOADER_PROJECT_DIR)/build/bootloader/bootloader.bin
PARTITION_TABLE_BIN := $(BOOTLOADER_PROJECT_DIR)/build/partition_table/partition-table.bin
BOOTLOADER_IMAGE_DEPS := $(BOOTLOADER_PROJECT_DIR)/CMakeLists.txt $(BOOTLOADER_PROJECT_DIR)/sdkconfig.defaults $(BOOTLOADER_PROJECT_DIR)/main/CMakeLists.txt $(BOOTLOADER_PROJECT_DIR)/main/dummy.c $(PARTITIONS_CSV)
BOOTLOADER_OFFSET := 0x0
PARTITION_TABLE_OFFSET := 0x8000
APP_OFFSET := 0x10000
TARGET_PREEMPT_SAFE_POINT_CAPABLE := 0
TARGET_SRCS_C := \
	$(TARGET_DIR)/src/gpio.c \
	$(TARGET_DIR)/src/interrupt.c \
	$(TARGET_DIR)/src/panel_gc9a01.c \
	$(TARGET_DIR)/src/timer.c \
	$(TARGET_DIR)/src/touch_cst816s.c \
	$(TARGET_DIR)/src/preempt_target.c
TARGET_SRCS_S :=
