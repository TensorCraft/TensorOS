# RISC-V Architecture Layer

This directory holds the RISC-V-specific boot and trap machinery currently used by TensorOS on ESP32-C3.

Files here are architecture-owned, not SoC-owned:

- `start.S`
- `context_switch.S`
- `trap.c`

These files may still rely on target-provided linker symbols and `soc.h`, but their ownership is now clearly separate from:

- common kernel policy in `kernel/`
- ESP32-C3 register routing and timer glue in `targets/esp32c3/`
