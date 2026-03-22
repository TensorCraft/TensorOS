# TensorOS bare-metal scaffold

> [!WARNING]
> This repository is still under active development. Core subsystems are incomplete, some hardware paths are experimental, and current functionality is not yet stable or feature-complete.

This repo is a freestanding kernel scaffold currently validated on ESP32-C3.

## Current Bring-Up Status

Current live blocker is the ESP32-C3 round GC9A01 display chain in TensorOS.

- Board under test is the common `ESP32-2424S012C-I-Y(B)`-style configuration:
  - `ESP32-C3`
  - `240x240` SPI round LCD
  - `GC9A01`
  - `CST816S/CST816D` touch
- Confirmed working:
  - panel init
  - white screen / pure fills
  - single pixels
  - small rectangles
  - color wheel / dense gradients
  - full TensorCore/reference 8x8 font lookup + bit parsing
  - `hello world` text rendering through dirty-rect local refresh
  - kernel boot and scheduler stability after reverting heavy probe paths
- Confirmed not solved yet:
  - the deeper layout-sensitive root cause behind flash `rodata` / large dense dispatch regressions
  - cleanup/refactor work to split the current GC9A01 panel logic into protocol, bus, and board layers
- Current engineering judgment:
  - font data itself is not the primary issue
  - a large dense character-rendering `switch` in `kernel/runtime_display_demo.c` was observed
    to fail under default `riscv32-esp-elf-gcc -Os` lowering, while the same logic works when
    compiled with `-fno-jump-tables -fno-tree-switch-conversion`
  - this strongly suggests a layout-sensitive latent bug in the current bare-metal runtime or
    display path, rather than a simple font-data error or a generic flash/RAM capacity issue
  - until the deeper root cause is fixed, the display demo keeps a file-local GCC workaround in
    the root `Makefile` for `kernel/runtime_display_demo.c`
  - in the current validated demo path, the `hello world` string is intentionally kept in
    DRAM-backed `static` storage inside `kernel/runtime_display_demo.c`; earlier attempts to rely
    on flash-backed string literals, larger flash rodata tables, or indirect rodata dispatch
    structures produced layout-sensitive regressions including wrong glyph selection and full
    white-screen failure
  - the validated text path now uses the TensorCore/reference 8x8 font table via array lookup +
    bit expansion into a small glyph buffer, then blits into a software backbuffer and commits
    only dirty rectangles through `display_surface_publish()` and
    `esp32c3_panel_gc9a01_flush_rect_pixels()`

The codebase is being prepared for multiple SoC targets through a `targets/<soc>/` layout.
It now also separates ISA-specific code under `arch/<isa>/`.

Current validated target:

- `esp32c3`

Reserved placeholder targets:

- `esp32c2`
- `esp32s3`

The firmware itself stays bare-metal:

- no ESP-IDF headers in the firmware itself
- no FreeRTOS
- hand-written `_start`
- vectored `mtvec` trap shell
- minimal SYSTIMER polling clock
- minimal console output

The firmware now mirrors logs to both:

- `UART0` for an external USB-UART adapter
- the ESP32-C3 built-in `USB Serial/JTAG` port over the same USB cable used for flashing

## Build

Make sure the ESP-IDF 6.0 cross-toolchain is on `PATH` so `riscv32-esp-elf-gcc` is available.

```bash
. $IDF_PATH/export.sh
make SOC_TARGET=esp32c3
```

`SOC_TARGET` defaults to `esp32c3` today.

## Target layout

Target-specific files now live under `targets/<soc>/`.

For example, `targets/esp32c3/` currently owns:

- `include/soc.h`
- `linker.ld`
- `target.mk`
- `src/interrupt.c`
- `src/timer.c`
- `packaging/partitions.csv`
- `packaging/bootloader_project/`

The RISC-V architecture layer now lives under `arch/riscv/` and currently owns:

- `arch.mk`
- `start.S`
- `context_switch.S`
- `trap.c`

The future Xtensa placeholder layer now lives under `arch/xtensa/` and currently owns:

- `arch.mk`
- `README.md`

The root `Makefile` selects the active target via `SOC_TARGET`, includes `targets/$(SOC_TARGET)/target.mk`, derives `ARCH_DIR := arch/$(TARGET_ARCH)`, and then includes `$(ARCH_DIR)/arch.mk`.

Each target now also owns its packaging artifact paths and can declare future target-local source files through `TARGET_SRCS_C` and `TARGET_SRCS_S`, while each architecture layer now owns:

- `CROSS`
- `ARCH_FLAGS`
- `ARCH_SRCS_C`
- `ARCH_SRCS_S`

Reserved future port space:

- `targets/esp32s3/`
- `arch/xtensa/`

Main outputs:

- `build/tensoros.elf`
- `build/tensoros.bin`
- `build/tensoros-app.bin`
- `build/tensoros.map`
- `build/tensoros.lst`

## Layered testing

This repo now keeps layered validation entry points under `tests/` so development can continue safely on the validated ESP32-C3 board while future targets remain placeholders.

Current entry points:

```bash
make test-build-contract
make test-image-layout
make test-process-core
make test-kmem
make test-runtime-policy
make test-preempt-contract
make test-runtime-manage
make test-runtime-loader
make test-runtime-service
make test-runtime-display
make test-runtime-resource
make test-runtime-input
make test-runtime-ui
make test-runtime-shell-path
make test-display-surface
make test-runtime-syscall
make test-runtime-fs
make test-runtime-fs-image
make test-runtime-fs-oracle
make test-runtime-fs-stress
make test-sync
make test-hw-smoke ESPPORT=/dev/cu.usbmodem11401 ESPBAUD=115200
```

The shared synchronization layer now includes allocator-backed wait queues, events, semaphores, a small non-recursive mutex object, and a small single-slot mailbox object. The kernel allocator now also exposes stable diagnostics for bytes in use, high-water marks, allocation failures, live-allocation counts, and block distribution snapshots. The current ESP32-C3 runtime path also includes a tiny shell-style management command layer with `help`, `runtime`, `ps`, `kmem`, `mailbox status`, `mailbox send <value>`, `mailbox recv`, `event signal`, `wake <pid>`, `kill <pid> [exit_code]`, `spawn <role> <task>`, `demo status`, `demo auto on|off`, and `demo profile off|smoke`, plus a minimal polled `UART0` line-input path that presents a `mgmt>` prompt. The validated hardware demo still routes mailbox and process-inspection behavior through that command path while keeping the cooperative scheduler path intact, and automatic demo activity now runs as a resettable default `smoke` profile so the existing smoke flow remains valid. That validated demo path now also includes a minimal service collaboration loop: the supervisor sends requests through a mailbox, wakes a service server through an event, the server updates shared state under a mutex, and the reply path returns through a mailbox plus semaphore completion signal. Alongside that runtime work, the repo now also has a first syscall-style interface scaffold with typed process, event, mailbox, and file handles, per-process handle ownership checks, `handle_dup` and `handle_info`, last-alias close semantics, and early `getpid`, `sleep`, `kill`, `mkdir`, `open`, `read`, `write`, `seek`, `stat`, `readdir`, `remove`, `rename`, `close`, `event_signal`, `mailbox_send`, and `mailbox_receive` style dispatch coverage. The repo also now includes a first in-memory file-system core with absolute-path lookup, directory creation, regular-file creation through `open(..., CREATE, ...)`, mode-checked `open/read/write/seek/stat/readdir/remove/rename/close`, more practical POSIX-leaning path and type error reporting, and a real syscall bridge for those file operations.

Looking ahead, TensorOS is also being steered toward an application-facing platform. Future system-call, file-system, process, and I/O interfaces should therefore preserve a practical POSIX-compatibility direction where it fits the TensorOS design, so higher layers such as the planned UI framework, Python interpreter, and application runtime do not depend on a one-off host API surface.

The rule for future work is to add the narrowest matching test layer whenever a new abstraction boundary is introduced.

The current `MS5` UI-runtime foundation now includes host-testable contracts for:

- `display service v0`
- `resource path v0`
- `input service v0`
- a small UI event-queue and timer primitive layer

These contracts are intentionally non-visual and host-first. They define bounded geometry, capability flags, package-shaped resource lookup paths, input-event envelopes, and UI timer delivery without changing the validated ESP32-C3 cooperative runtime path or claiming a finished rendering stack.

Alongside the UI-facing runtime work, the tree now also has a first FS-style shell `PATH` resolver that searches bare command names through colon-separated directories such as `/system/bin:/bin` and resolves them to absolute paths such as `/system/bin/ls`. The current management shell exposes that narrow contract through `path` and `which <name>`.

The current `MS6` groundwork also now includes a target-owned bare-metal GC9A01 display path, a target-owned bare-metal CST816S touch path, and a host-testable `display_surface` core for dirty-rectangle plus atomic-present rendering. The render core is where future UI work should build partial refresh and efficient frame publication, while the board-wired panel and touch implementations stay under `targets/esp32c3/`.

The current staged path from scaffold to final product is documented in [doc/roadmap.md](/Users/tensorcraft/Projects/TensorOS/doc/roadmap.md).

Current mode knobs:

```bash
make SCHED_MODEL=cooperative RUNTIME_MODEL=multiprocess
make SCHED_MODEL=cooperative RUNTIME_MODEL=single-foreground
```

On ESP32-C3, `SCHED_MODEL=preemptive` is intentionally rejected to preserve the validated cooperative runtime path.
The repository now also includes a preemptive-scheduler skeleton and host-side contract test, but not a validated preemptive runtime path on ESP32-C3.
That skeleton now includes safe-point bookkeeping, nested `preempt_disable()` or `preempt_enable()` state, and target-owned capability hooks for future non-ESP32 ports, while ESP32-C3 remains cooperative-only.

## Flashable image flow

This repo includes target-owned packaging for a standard ESP32-C3 flash layout:

- bootloader at `0x0`
- partition table at `0x8000`
- TensorOS app image at `0x10000`

Useful targets today:

```bash
make flash ESPPORT=/dev/cu.usbmodem11401
make flash-image
make flash-full ESPPORT=/dev/cu.usbmodem11401
make capture-serial ESPPORT=/dev/cu.usbmodem11401
make monitor-usb ESPPORT=/dev/cu.usbmodem11401
```

Important behavior:

- `make flash` now restores the original direct-app debug flow and writes the app image at `0x0` in `app-only` mode
- `make flash-image` builds the merged bootloader + partition + app image artifact
- `make flash` and `make flash-full` now immediately run an 8-second delayed serial capture after flashing
- `make flash-full` writes the merged image to flash, but that path is not the default development workflow yet
- `tests/capture_serial.py` now retries opening the serial port so post-reset USB re-enumeration is less likely to hide early boot output

Current bring-up note:

- A March 18, 2026 boot-chain investigation found that an earlier `make flash` implementation had incorrectly written the app image at `0x0`, which can overwrite the bootloader region.
- The repository now restores the intended `app-only` flash offset to `0x10000`.
- During that investigation, directly flashing the app image at `0x0` was observed to produce a running TensorOS prototype with live serial output.
- A later March 18, 2026 rerun of `make flash-full ESPPORT=/dev/cu.usbmodem11401 ESPBAUD=115200` showed that the standard boot chain now reaches:
  - ESP-ROM banner
  - ESP-IDF 2nd-stage bootloader
  - partition-table parsing
  - app image load from `0x10000`
- A follow-up March 18, 2026 linker-layout fix moved TensorOS executable text into mapped IROM (`0x42000020`) while keeping app metadata and rodata in DROM.
- After reflashing with that image via `make flash-full ESPPORT=/dev/cu.usbmodem11401 ESPBAUD=115200`, the standard boot chain advanced further:
  - bootloader image parsing now reports a real IROM segment instead of `text starts from paddr=0x00000000, vaddr=0x00000000, size=0x0`
  - bootloader handoff now maps:
    - rodata from `paddr=0x00010020` to `vaddr=0x3c000020`
    - text from `paddr=0x00020020` to `vaddr=0x42000020`
  - bootloader reaches `start: 0x42000020` without the earlier `mmu_hal_map_region()` abort
- So the original bootloader->app MMU mapping failure is resolved for the standard packaged path.
- The current remaining hardware issue is post-handoff app behavior: after the jump to `0x42000020`, USB serial output becomes garbled instead of producing the expected TensorOS boot log.
- Treat `flash-full` as the architecture target and `0x0` direct app flashing only as a temporary debugging experiment, not a documented boot contract.
- The practical next debugging stage is now inside TensorOS startup/runtime initialization after a successful standard boot handoff, not in the bootloader MMU mapper.
- A later same-day user-directed rollback restored the original `0x0` direct-app `make flash` flow as the active development baseline, while keeping the standard packaged boot-chain work documented in this file and in `progress.md`.
- The standard packaged path remains backed up as proven progress, but it is not the current default flashing workflow after that rollback.
- A subsequent same-day decision switched active work back to the standard packaged boot chain, and the latest validated `flash-full` capture again shows TensorOS reaching the post-spawn kernel marker `rtc_store0=0x0000c010`.
- A later reset-after-run validation with the same packaged image showed the next concrete runtime fact:
  - after allowing the freshly flashed image to run for several seconds and then forcing another reset, bootloader trace reported `rtc_store0=0x0000f401`
  - that marker means pid `1` reached the scheduler task-bootstrap path
  - the same capture still showed `rtc_store3=0xa0000001`, so `task_a` is the task that is actually running
  - the packaged-path blocker is therefore no longer "can the first task start?" but "why do later runnable processes, especially FSSTRESS, not get time after pid 1 starts yielding?"
- A later March 18, 2026 standard-path FS stress investigation pushed the live blocker much further:
  - standard packaged boot now reliably reaches `process_fs_stress()`
  - `runtime_fs_stress_init_fs_chunked(...)` completes on real hardware
  - the first stage-1 probe and first `runtime_fs_validate()` gate both pass on real hardware
  - the active blocker is now narrowly:
    - `process_fs_stress()` step 2
    - `runtime_fs_mkdir(&g_runtime_fs_stress_state.fs, current_pid, "/stress", 0755u)`
  - the current reset-after-run signature remains:
    - `rtc_store0=0x0000f5e2`
    - `rtc_store1=0x00000008`
    - `rtc_store2=0x3c000a70`
    - `rtc_store3=0x65727473`
    - `rtc_store4=0x00280028`
    - `rtc_store5=0x4c4b4c4b`
    - `rtc_store6=0x3fc81990`
    - `rtc_store7=0x00000613`
  - during that same investigation:
    - the final linked `runtime_fs_mkdir()` success-return path was repaired and revalidated in disassembly so it now routes through a real helper return path instead of falling through a stale epilogue value
    - an additional real-hardware rerun then tested step 2 with a temporary local interrupt-disable window around the `runtime_fs_mkdir()` call
    - that interrupt-window experiment did not change the reset-after-run signature above
  - practical interpretation:
    - the blocker is no longer explained by the old `runtime_fs_mkdir()` success-return bug alone
    - and is less likely to be explained solely by ordinary post-enable interrupt clobber between the step-2 call and the immediate status capture
    - the next debugging cut should stay narrowly focused on the step-2 call boundary and its immediate invariants on real hardware

`make flash-image` produces:

- `targets/esp32c3/packaging/bootloader_project/build/partition_table/partition-table.bin`
- `targets/esp32c3/packaging/bootloader_project/build/bootloader/bootloader.bin`
- `build/tensoros-app.bin`
- `build/tensoros-flash.bin`

If you want to test without writing flash:

```bash
make load-ram ESPPORT=/dev/cu.usbmodem11401
```

## USB logging

Once the board is booted, logs should appear on the built-in USB CDC port:

```bash
make monitor-usb ESPPORT=/dev/cu.usbmodem11401
```

The firmware prints lines like:

```text
[boot] TensorOS bare-metal scaffold
[boot] uart0 ready
[boot] usb-serial-jtag ready
[boot] systimer ready
[boot] jump -> kernel
[kern] kernel start
counter=1
```

## External UART0 wiring

The same log stream is also mirrored to `UART0`, which is useful if:

- the USB CDC console is unavailable
- you want a second monitor during bring-up
- you want to compare bootloader USB logs with app UART logs

Typical ESP32-C3 `UART0` wiring:

- ESP32-C3 `GPIO21` (`U0TXD`) -> adapter `RX`
- ESP32-C3 `GPIO20` (`U0RXD`) -> adapter `TX`
- ESP32-C3 `GND` -> adapter `GND`

Use a 3.3V USB-UART adapter. Do not connect 5V logic directly.

To monitor via the external adapter:

```bash
make monitor-uart0 PORT=/dev/cu.usbserial-XXXX
```

Baud rate for both monitor paths is `115200`.

## Packaging notes

The firmware itself stays bare-metal. The flash boot chain is provided by a tiny ESP-IDF-based packaging helper project inside each target directory, currently `targets/esp32c3/packaging/bootloader_project`.

That helper project is only used to build:

- the second-stage bootloader
- the partition table
- merged flash images for future target-aware boot flows

## Warning

ESP32-C3 IRAM and DRAM are aliases of the same internal SRAM through different buses.

This repo must keep a DRAM hole that mirrors the occupied IRAM range before placing `.data`, `.bss`, task stacks, or the main stack. If that gap is removed, startup BSS clearing can silently overwrite the vectored `mtvec` table and trap entry image, which shows up on hardware as interrupt entry failures or `illegal instruction` at vector-slot addresses.

## Next step ideas

1. Add a true preemptive scheduler backend for future non-ESP32 targets without violating the ESP32-C3 timer ISR no-switch contract.
2. Extend the minimal service-collaboration path into a less demo-driven service model with clearer lifecycle and object inspection.
3. Extend the new runtime-management command path into a richer shell or syscall surface.
4. Introduce PMP/PMS setup for privilege separation.
Latest hardware status on the standard packaged boot chain:

- `bootloader @ 0x0`, partition table @ `0x8000`, app @ `0x10000` is still the active and verified boot path.
- Real hardware repeatedly confirms:
  - ESP-ROM -> ESP-IDF second-stage bootloader
  - partition table parse
  - app load from `0x10000`
  - DROM/IROM mapping
  - jump to `0x42000020`
- Post-run reset capture still shows runtime breadcrumbs proving the kernel and task runtime are alive, and that
  `FSSTRESS` enters its start marker (`rtc_store0=0xF500`, `rtc_store2=8`, `rtc_store3=0xF5000001`).
- The latest narrowing pass pushed that further:
  - current real-hardware stop point is `rtc_store0=0xF502`
  - in the current image, `F502` is the marker immediately before `runtime_fs_init(&g_runtime_fs_stress_state.fs)` returns
  - `F503` is the first marker after `runtime_fs_init()`
- Attempts that did not clear the stall:
  - simplifying `FSSTRESS` to a direct FS smoke path
  - rewriting `runtime_fs_init()` to a simple byte-wise zero loop
  - temporarily disabling interrupts around `runtime_fs_init()`
- March 18, 2026 diagnostic probe build:
  - `g_runtime_fs_stress_state` is in `.bss` at `0x3fc81968`, and its `fs` window spans `0x3fc81968..0x3fc82ca7` (`0x1340` bytes)
  - the current diagnostic image no longer calls `runtime_fs_init()` in `FSSTRESS`; it performs 10 sparse single-byte touches across that `fs` window and writes per-touch RTC markers `F520..F533`, with `F53F` reserved for probe completion
  - validated host-side before flashing with:
    - `bash tests/check_runtime_fs.sh`
    - `bash tests/check_runtime_fs_image.sh`
    - `bash tests/check_runtime_fs_stress.sh`
  - flashed and exercised on real hardware with:
    - `make flash-full ESPPORT=/dev/cu.usbmodem11401 ESPBAUD=115200 SERIAL_CAPTURE_SECONDS=10`
    - `python3 -m esptool --chip esp32c3 --port /dev/cu.usbmodem11401 --baud 115200 chip_id`
    - `python3 /Users/tensorcraft/Projects/TensorOS/tests/capture_serial.py /dev/cu.usbmodem11401 115200 10`
  - result so far:
    - the automatic capture during `flash-full` still showed the previously retained pre-reset breadcrumb set headed by `rtc_store0=0xF502`
    - the required post-run reset capture after `chip_id` produced no fresh serial bytes, so this pass did not yet prove which sparse touch, if any, is the new failing address
- March 18, 2026 follow-up repair pass:
  - `tests/capture_serial.py` now survives USB re-enumeration, and when a post-reset capture window is silent it retries by issuing one internal `esptool chip_id` reset and reconnecting before giving up
  - the `FSSTRESS` init path in [kernel.c](/Users/tensorcraft/Projects/TensorOS/kernel/kernel.c) is no longer a monolithic `runtime_fs_init()` call on hardware:
    - it now clears `g_runtime_fs_stress_state.fs` in 32-byte chunks
    - the chunked init runs with global interrupts disabled
    - the handoff back to normal interrupt state uses `interrupts_enable()` instead of a raw CSR set so pending systimer state is explicitly cleared before re-enable
  - real post-run reset captures now prove the original blocker moved:
    - `F53F` with `rtc_store6=0x00001340` confirms the full `0x1340`-byte `fs` window initializes successfully on real hardware
    - `F540` confirms execution advances past `F503` and the immediate `runtime_fs_validate(&g_runtime_fs_stress_state.fs)` call while interrupts are still masked
    - a later retained marker of `F5EE` with `rtc_store2=0x00000001` and `rtc_store3=0x00000001` proves the code now reaches `fs_done`, but fails at `last_step=1`, which is the first `runtime_fs_validate()` expectation immediately after init
- Current live blocker on real hardware is therefore no longer the `F502 -> F503` init window, and it is no longer the raw post-init `runtime_fs_validate()` body either.
- Latest March 18, 2026 narrowing:
  - retained marker `F541` proves execution now advances through:
    - full chunked `fs` init
    - immediate post-init validation call setup
    - formal interrupt re-enable via `interrupts_enable()`
  - latest required flash + post-run reset capture now reports:
    - `rtc_store0=0x0000f5ee`
    - `rtc_store1=0x0000e1fe`
    - `rtc_store2=0xffffffeau`
    - `rtc_store3=0x00000000`
    - `rtc_store6=0xffffffeau`
    - `rtc_store7=0x00000000`
  - interpretation:
    - `store1=0xE1FE` means the hand-written stage-1 mirror passed every structural invariant and only the final raw `runtime_fs_validate(fs)` return was being reported
    - `store2=0xFFFFFFEA` / `store6=0xFFFFFFEA` confirms that raw return is still `-22` (`RUNTIME_SYSCALL_STATUS_EINVAL`) before `interrupts_enable()`
    - the earlier fake step-1 mismatch after `F541` was caused by the `status` value becoming invalid across the `interrupts_enable()` boundary, not by the validation body returning a random code
  - so the active real-hardware blocker is now:
    - `runtime_fs_validate()` really does return `EINVAL` immediately after chunked init while interrupts are still masked
    - in addition, the old step-1 compare location after `interrupts_enable()` was unsafe for preserving that return value
- Latest March 18, 2026 follow-up narrowing:
  - branch-level tracing inside `runtime_fs_validate()` itself showed the raw pre-step-1 failure was rooted in the root-directory check path (`D001`), even though the retained bytes still showed:
    - `used=1`
    - `type=2`
    - `parent_index=0`
    - `name[0]='/'`
    - `name[1]='\\0'`
  - replacing the root `name_equals(fs->nodes[0].name, "/")` validation with direct byte checks moved the real-hardware stop point forward again
  - the latest required post-run reset capture after that fix now reports:
    - `rtc_store0=0x0000f5ee`
    - `rtc_store1=0x00000000`
    - `rtc_store2=0x00000001`
    - `rtc_store3=0x00000002`
  - interpretation:
    - step 1 now passes on hardware
    - the standard chain reaches the next FS action and now fails at `last_step=2`
    - so the immediate blocker is no longer the first post-init validation; it has advanced to the next FS operation after that validation
- What is still not proven on hardware is the final FS success marker `F5AA`; the current blocker has advanced past the first post-init validation and now sits at step 2, i.e. the next FS operation after that validation. The `interrupts_enable()` handoff remains a separately identified value-preservation hazard, but it is no longer the live reason step 1 fails.
