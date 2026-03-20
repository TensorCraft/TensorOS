# Architecture Layer

This directory owns ISA-specific implementation code that should be shared by multiple SoC targets using the same CPU architecture.

Current subdirectories:

- `riscv/` for the validated ESP32-C3 path

Planned future subdirectories:

- `xtensa/` for ESP32-S3 bring-up work

Ownership rule:

- put `_start`, trap-entry ABI, context-switch assembly, and ISA CSR/exception handling here
- put architecture toolchain prefixes and low-level compiler flags in `arch/<isa>/arch.mk`
- keep SoC register maps, linker scripts, flash layout, and interrupt-matrix routing under `targets/<soc>/`
