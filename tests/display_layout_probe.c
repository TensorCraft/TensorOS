#include <stdint.h>

/*
 * Codegen/layout probe for the ESP32-C3 GC9A01 text-path regression.
 *
 * The point of this file is not runtime behavior. It exists so we can compile
 * a few tiny source-shape variants and inspect which ones stay in DRAM-backed
 * writable storage vs which ones lower into flash-backed rodata tables.
 */

static char g_display_google_g_upper[] = "G";
static char g_display_google_o_first[] = "o";
static char g_display_google_o_second[] = "o";
static char g_display_google_g_lower[] = "g";
static char g_display_google_l[] = "l";
static char g_display_google_e[] = "e";
static uint16_t g_google_blue = 0x001Fu;
static uint16_t g_google_red = 0xF800u;
static uint16_t g_google_yellow = 0xFFE0u;
static uint16_t g_google_green = 0x07E0u;

extern void display_layout_probe_sink_letter(unsigned index, const char *text,
                                             uint16_t color);

void display_layout_probe_stable_explicit(void) {
  display_layout_probe_sink_letter(0u, g_display_google_g_upper, g_google_blue);
  display_layout_probe_sink_letter(1u, g_display_google_o_first, g_google_red);
  display_layout_probe_sink_letter(2u, g_display_google_o_second,
                                   g_google_yellow);
  display_layout_probe_sink_letter(3u, g_display_google_g_lower, g_google_blue);
  display_layout_probe_sink_letter(4u, g_display_google_l, g_google_green);
  display_layout_probe_sink_letter(5u, g_display_google_e, g_google_red);
}

void display_layout_probe_local_arrays_loop(void) {
  char letters[6][2] = {"G", "o", "o", "g", "l", "e"};
  uint16_t colors[6] = {0x001Fu, 0xF800u, 0xFFE0u, 0x001Fu, 0x07E0u, 0xF800u};

  for (unsigned i = 0u; i < 6u; ++i) {
    display_layout_probe_sink_letter(i, letters[i], colors[i]);
  }
}

void display_layout_probe_rodata_dispatch_loop(void) {
  static const char *const letters[6] = {"G", "o", "o", "g", "l", "e"};
  static const uint16_t colors[6] = {0x001Fu, 0xF800u, 0xFFE0u,
                                     0x001Fu, 0x07E0u, 0xF800u};

  for (unsigned i = 0u; i < 6u; ++i) {
    display_layout_probe_sink_letter(i, letters[i], colors[i]);
  }
}

uint16_t display_layout_probe_switch_color(unsigned index) {
  switch (index) {
  case 0u:
    return 0x001Fu;
  case 1u:
    return 0xF800u;
  case 2u:
    return 0xFFE0u;
  case 3u:
    return 0x001Fu;
  case 4u:
    return 0x07E0u;
  case 5u:
    return 0xF800u;
  default:
    return 0u;
  }
}

void display_layout_probe_switch_dispatch_loop(void) {
  static const char *const letters[6] = {"G", "o", "o", "g", "l", "e"};

  for (unsigned i = 0u; i < 6u; ++i) {
    display_layout_probe_sink_letter(i, letters[i],
                                     display_layout_probe_switch_color(i));
  }
}
