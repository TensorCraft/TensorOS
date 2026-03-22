# Display Bring-Up Notes

## Stable Baseline

- Board under test: common `ESP32-2424S012C-I-Y(B)` style board
- SoC: `ESP32-C3`
- Panel: `GC9A01`
- Stable panel color order on this board: `BGR`
- Stable `MADCTL` setting in the current driver: `0x08`

Current validated behavior:

- direct panel fills work
- direct panel quadrant colors validate as `red/green` over `blue/white`
- software dirty-rect flush through `display_surface_publish()` and
  `esp32c3_panel_gc9a01_flush_rect_pixels()` also works for simple quadrant tests
- centered colored `Google` wordmark renders correctly through the software surface path

Relevant files:

- `targets/esp32c3/src/panel_gc9a01.c`
- `targets/esp32c3/include/esp32c3_panel_gc9a01.h`
- `kernel/runtime_display_demo.c`

## Proven Diagnostics

The following diagnostic sequence was useful and should be preserved conceptually:

1. Use direct hardware quadrants to validate basic panel write behavior.
2. Use a different software-surface quadrant pattern to validate that flush really overrides the
   direct draw result.
3. If red and blue are swapped while green and white still look correct, treat that first as panel
   color-order trouble, not an application RGB565 constant bug.

Observed stable interpretation:

- `red -> blue`
- `blue -> red`
- `green -> green`
- `white -> white`

That pattern matched `RGB/BGR` mismatch and was fixed with `MADCTL=0x08`.

## Layout-Sensitive Failure Pattern

The remaining problem is not a generic panel failure. It is layout-sensitive and seems tied to
where data ends up and how the compiler lowers certain code shapes.

Observed failure modes:

- wrong glyphs appearing even when the font table itself is correct
- blanks or partial text instead of the expected word
- wrong colors when using local text/color arrays
- full white-screen failures in earlier variants

Shapes that proved risky:

- flash-backed string literals
- larger flash `rodata` tables
- indirect `rodata` dispatch structures
- large dense `switch` logic under default `-Os` lowering
- local array-driven text/color loops in the display demo

Shapes that proved stable:

- file-local `static` storage for strings or per-letter glyph sources
- TensorCore/reference `8x8` font lookup via direct array indexing
- bit expansion into a small local glyph pixel buffer
- explicit per-letter drawing instead of a loop over a local string/color table
- the existing `-fno-jump-tables -fno-tree-switch-conversion` workaround for
  `kernel/runtime_display_demo.c`

Concrete example from bring-up:

- A colored `Google` wordmark implemented as a loop over a local text buffer and local color table
  rendered incorrectly as partial text like `C`, blanks, and wrong colors.
- Rewriting it to use file-local `static` per-letter strings and explicit per-letter draw calls
  made it render correctly on hardware.

## Safe Working Rules

Until the deeper root cause is fixed:

- keep a known-good visual baseline in `kernel/runtime_display_demo.c`
- avoid reverting `MADCTL=0x08` unless re-proving the panel color order on hardware
- prefer file-local `static` storage for display-demo text inputs
- avoid introducing large local lookup tables or new dense dispatch logic into the demo path
- verify every experiment can be rolled back to the current colored `Google` baseline quickly

## Recommended Next Investigation

Focus on explaining the layout sensitivity rather than re-debugging panel colors.

Good next targets:

- compare generated assembly and section placement for stable vs unstable text paths
- compare addresses/sections for working file-local `static` strings vs failing local arrays
- inspect whether affected data lands in flash, DRAM, or a problematic boundary region
- minimize to the smallest repro that flips from working to broken
- keep direct panel and software flush diagnostics available so panel regressions can be ruled out
  quickly
