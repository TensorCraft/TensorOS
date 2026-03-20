# ESP32-S3 Target Placeholder

This directory reserves the target-owned space for a future ESP32-S3 port.

Important constraint:

- ESP32-S3 is expected to require an `arch/xtensa/` implementation and must not reuse the current RISC-V `arch/riscv/` entry, trap, or context-switch code.

Before selecting `SOC_TARGET=esp32s3`, add at minimum:

- `target.mk`
- `include/soc.h`
- `linker.ld`
- `packaging/partitions.csv`
- `packaging/bootloader_project/`
- real `arch/xtensa/` runtime implementation files beyond the current placeholder `arch.mk`

Expected future ownership for ESP32-S3:

- target-local interrupt routing glue
- target-local timer glue
- Xtensa-compatible boot-image packaging details
