PROJECT := tensoros
BUILD_DIR := build
IDF_PATH ?= $(HOME)/esp/esp-idf
IDF_PYTHON ?= $(HOME)/.espressif/python_env/idf6.0_py3.13_env/bin/python
IDF_PY := IDF_PATH=$(IDF_PATH) $(IDF_PYTHON) $(IDF_PATH)/tools/idf.py
ESPTOOL ?= esptool.py
ESPPORT ?= /dev/cu.usbmodem11401
ESPBAUD ?= 460800
MONITOR_BAUD ?= 115200
FLASH_SIZE ?= 4MB
SOC_TARGET ?= esp32c3
TARGET_DIR := targets/$(SOC_TARGET)
FLASH_LAYOUT ?= app-only
SERIAL_CAPTURE_SECONDS ?= 8
SCHED_MODEL ?= cooperative
RUNTIME_MODEL ?= multiprocess
TARGET_PREEMPT_SAFE_POINT_CAPABLE ?= 0
RUNTIME_ENABLE_FS_STRESS ?= 0

include $(TARGET_DIR)/target.mk

ARCH_DIR := arch/$(TARGET_ARCH)
include $(ARCH_DIR)/arch.mk

APP_BIN := $(BUILD_DIR)/$(PROJECT)-app.bin
FLASH_IMAGE_BIN := $(BUILD_DIR)/$(PROJECT)-flash.bin
BUILD_CONFIG_STAMP := $(BUILD_DIR)/.build-config

CC := $(CROSS)gcc
OBJDUMP := $(CROSS)objdump
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size

ifeq ($(SCHED_MODEL),cooperative)
SCHED_MODEL_DEFINES := -DCONFIG_SCHEDULER_COOPERATIVE=1 -DCONFIG_SCHEDULER_PREEMPTIVE=0
else ifeq ($(SCHED_MODEL),preemptive)
SCHED_MODEL_DEFINES := -DCONFIG_SCHEDULER_COOPERATIVE=0 -DCONFIG_SCHEDULER_PREEMPTIVE=1
else
$(error Unsupported SCHED_MODEL '$(SCHED_MODEL)'; use cooperative or preemptive)
endif

ifeq ($(RUNTIME_MODEL),multiprocess)
RUNTIME_MODEL_DEFINES := -DCONFIG_RUNTIME_SINGLE_FOREGROUND=0 -DCONFIG_RUNTIME_MULTIPROCESS=1
else ifeq ($(RUNTIME_MODEL),single-foreground)
RUNTIME_MODEL_DEFINES := -DCONFIG_RUNTIME_SINGLE_FOREGROUND=1 -DCONFIG_RUNTIME_MULTIPROCESS=0
else
$(error Unsupported RUNTIME_MODEL '$(RUNTIME_MODEL)'; use multiprocess or single-foreground)
endif

COMMON_FLAGS := $(ARCH_FLAGS) $(SCHED_MODEL_DEFINES) $(RUNTIME_MODEL_DEFINES) -ffreestanding -fno-builtin -fdata-sections -ffunction-sections -Os -g3 -Wall -Wextra -Werror -I$(TARGET_INCLUDE_DIR) -Iinclude
COMMON_FLAGS += -DCONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE=$(TARGET_PREEMPT_SAFE_POINT_CAPABLE)
COMMON_FLAGS += -DCONFIG_RUNTIME_ENABLE_FS_STRESS=$(RUNTIME_ENABLE_FS_STRESS)
EXTRA_CFLAGS ?=
EXTRA_ASFLAGS ?=
CFLAGS := $(COMMON_FLAGS) -std=c11 $(EXTRA_CFLAGS)
ASFLAGS := $(COMMON_FLAGS) -x assembler-with-cpp $(EXTRA_ASFLAGS)
LDFLAGS := $(ARCH_FLAGS) -nostdlib -Wl,-T,$(LINKER_SCRIPT) -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map -Wl,--gc-sections

COMMON_SRCS_C := \
	boot/app_desc.c \
	boot/boot.c \
	kernel/freestanding.c \
	kernel/kmem.c \
	kernel/runtime_policy.c \
	kernel/runtime_catalog.c \
	kernel/runtime_loader.c \
	kernel/runtime_service.c \
	kernel/display_surface.c \
	kernel/runtime_display_demo.c \
	kernel/preempt.c \
	kernel/runtime_manage.c \
	kernel/runtime_syscall.c \
	kernel/runtime_fs_image.c \
	kernel/runtime_fs.c \
	kernel/wait_queue.c \
	kernel/event.c \
	kernel/semaphore.c \
	kernel/mutex.c \
	kernel/mailbox.c \
	kernel/scheduler.c \
	kernel/process_core.c \
	kernel/kernel.c \
	drivers/console.c \
	drivers/uart.c \
	drivers/usb_serial_jtag.c \
	drivers/systimer.c
SRCS_C := $(COMMON_SRCS_C) $(TARGET_SRCS_C)
SRCS_C += $(ARCH_SRCS_C)
SRCS_S := $(ARCH_SRCS_S) $(TARGET_SRCS_S)
OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS_C)) $(patsubst %.S,$(BUILD_DIR)/%.o,$(SRCS_S))

ifeq ($(TARGET_SUPPORTED),0)
$(error $(TARGET_UNSUPPORTED_REASON))
endif

.PHONY: all clean check-toolchain app-image bootloader-image flash-image flash flash-full capture-serial load-ram monitor-usb monitor-uart0 test-build-contract test-image-layout test-process-core test-kmem test-runtime-policy test-runtime-catalog test-runtime-loader test-runtime-service test-runtime-display test-runtime-resource test-runtime-input test-runtime-ui test-runtime-shell-path test-display-surface test-preempt-contract test-runtime-manage test-runtime-syscall test-runtime-fs test-runtime-fs-image test-runtime-fs-oracle test-runtime-fs-stress test-runtime-stability-baseline test-sync test-hw-smoke test-hw-fs FORCE

all: check-toolchain $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).lst
	$(SIZE) $(BUILD_DIR)/$(PROJECT).elf

check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { \
		echo "error: $(CC) not found in PATH"; \
		echo "hint: source $$IDF_PATH/export.sh from your ESP-IDF 6.0 environment first"; \
		exit 1; \
	}

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS) $(LINKER_SCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(OBJCOPY) -O binary $@ $(BUILD_DIR)/$(PROJECT).bin

$(BUILD_DIR)/$(PROJECT).lst: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJDUMP) -d -S $< > $@

$(BUILD_CONFIG_STAMP): FORCE
	@set -eu; \
	mkdir -p $(dir $@); \
	tmp_file='$@.tmp'; \
	printf '%s\n' \
	  'SOC_TARGET=$(SOC_TARGET)' \
	  'SCHED_MODEL=$(SCHED_MODEL)' \
	  'RUNTIME_MODEL=$(RUNTIME_MODEL)' \
	  'COMMON_FLAGS=$(COMMON_FLAGS)' \
	  'EXTRA_CFLAGS=$(EXTRA_CFLAGS)' \
	  'EXTRA_ASFLAGS=$(EXTRA_ASFLAGS)' \
	  'CFLAGS=$(CFLAGS)' \
	  'ASFLAGS=$(ASFLAGS)' \
	  'LDFLAGS=$(LDFLAGS)' > "$$tmp_file"; \
	if [ ! -f '$@' ] || ! cmp -s "$$tmp_file" '$@'; then \
	  mv "$$tmp_file" '$@'; \
	else \
	  rm -f "$$tmp_file"; \
	fi

$(BUILD_DIR)/%.o: %.c $(BUILD_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S $(BUILD_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

app-image: $(APP_BIN)

$(APP_BIN): $(BUILD_DIR)/$(PROJECT).elf
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) elf2image --flash_mode dio --flash_freq 40m --flash_size $(FLASH_SIZE) --use-segments --output $@ $<

bootloader-image: $(BOOTLOADER_BIN) $(PARTITION_TABLE_BIN)

$(BOOTLOADER_BIN) $(PARTITION_TABLE_BIN): $(BOOTLOADER_IMAGE_DEPS)
	$(IDF_PY) -C $(BOOTLOADER_PROJECT_DIR) -DIDF_TARGET=$(IDF_TARGET) build bootloader partition-table

flash-image: $(FLASH_IMAGE_BIN)

$(FLASH_IMAGE_BIN): bootloader-image $(APP_BIN)
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) merge_bin --flash_mode dio --flash_freq 40m --flash_size $(FLASH_SIZE) --output $@ $(BOOTLOADER_OFFSET) $(BOOTLOADER_BIN) $(PARTITION_TABLE_OFFSET) $(PARTITION_TABLE_BIN) $(APP_OFFSET) $(APP_BIN)

flash: $(if $(filter full,$(FLASH_LAYOUT)),flash-image,app-image)
ifeq ($(FLASH_LAYOUT),full)
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) --port $(ESPPORT) --baud $(ESPBAUD) write-flash 0x0 $(FLASH_IMAGE_BIN)
else
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) --port $(ESPPORT) --baud $(ESPBAUD) write-flash $(APP_OFFSET) $(APP_BIN)
endif
	@$(MAKE) capture-serial ESPPORT=$(ESPPORT) MONITOR_BAUD=$(MONITOR_BAUD) SERIAL_CAPTURE_SECONDS=$(SERIAL_CAPTURE_SECONDS)

flash-full: flash-image
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) --port $(ESPPORT) --baud $(ESPBAUD) write-flash 0x0 $(FLASH_IMAGE_BIN)
	@$(MAKE) capture-serial ESPPORT=$(ESPPORT) MONITOR_BAUD=$(MONITOR_BAUD) SERIAL_CAPTURE_SECONDS=$(SERIAL_CAPTURE_SECONDS)

capture-serial:
	@python3 ./tests/capture_serial.py "$(ESPPORT)" "$(MONITOR_BAUD)" "$(SERIAL_CAPTURE_SECONDS)" | \
		{ if IFS= read -r first_line; then \
			printf '%s\n' "$$first_line"; \
			cat; \
		else \
			printf '[no serial output captured]\n'; \
		fi; }

load-ram: app-image
	$(ESPTOOL) --chip $(ESPTOOL_CHIP) --port $(ESPPORT) --baud $(ESPBAUD) --before default-reset --after no-reset --no-stub load-ram $(APP_BIN)

monitor-usb:
	python3 -m serial.tools.miniterm $(ESPPORT) $(MONITOR_BAUD)

monitor-uart0:
	python3 -m serial.tools.miniterm $(PORT) $(MONITOR_BAUD)

test-build-contract:
	./tests/check_build_contract.sh

test-image-layout:
	./tests/check_image_layout.sh

test-process-core:
	./tests/check_process_core.sh

test-kmem:
	./tests/check_kmem.sh

test-runtime-policy:
	./tests/check_runtime_policy.sh

test-runtime-catalog:
	./tests/check_runtime_catalog.sh

test-runtime-loader:
	./tests/check_runtime_loader.sh

test-runtime-service:
	sh ./tests/check_runtime_service.sh

test-runtime-display:
	sh ./tests/check_runtime_display.sh

test-runtime-resource:
	sh ./tests/check_runtime_resource.sh

test-runtime-input:
	sh ./tests/check_runtime_input.sh

test-runtime-ui:
	sh ./tests/check_runtime_ui.sh

test-runtime-shell-path:
	sh ./tests/check_runtime_shell_path.sh

test-display-surface:
	sh ./tests/check_display_surface.sh

test-preempt-contract:
	./tests/check_preempt_contract.sh

test-runtime-manage:
	./tests/check_runtime_manage.sh

test-runtime-syscall:
	./tests/check_runtime_syscall.sh

test-runtime-fs:
	./tests/check_runtime_fs.sh

test-runtime-fs-image:
	./tests/check_runtime_fs_image.sh

test-runtime-fs-oracle:
	sh ./tests/check_runtime_fs_oracle.sh

test-runtime-fs-stress:
	sh ./tests/check_runtime_fs_stress.sh

test-runtime-stability-baseline:
	./tests/check_runtime_stability_baseline.sh

test-sync:
	./tests/check_sync.sh

test-hw-smoke:
	RUNTIME_MODEL="$(RUNTIME_MODEL)" SCHED_MODEL="$(SCHED_MODEL)" ESPPORT="$(ESPPORT)" ESPBAUD="$(ESPBAUD)" ./tests/hw_smoke_esp32c3.sh

test-hw-fs:
	RUNTIME_MODEL="$(RUNTIME_MODEL)" SCHED_MODEL="$(SCHED_MODEL)" ESPPORT="$(ESPPORT)" ESPBAUD="$(ESPBAUD)" sh ./tests/hw_fs_esp32c3.sh

clean:
	rm -rf $(BUILD_DIR)
