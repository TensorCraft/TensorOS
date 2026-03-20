#include "runtime_display_demo.h"

#include "display_surface.h"
#include "esp32c3_panel_gc9a01.h"
#include "esp32c3_touch_cst816s.h"
#include "runtime.h"
#include "soc.h"

#include <stdint.h>

enum {
  DISPLAY_DEMO_BACKGROUND = 0xFFFFu,
  DISPLAY_DEMO_FOREGROUND = 0x0000u,
  DISPLAY_DEMO_WIDTH = ESP32C3_PANEL_GC9A01_WIDTH,
  DISPLAY_DEMO_HEIGHT = ESP32C3_PANEL_GC9A01_HEIGHT,
  DISPLAY_DEMO_GLYPH_W = 8u,
  DISPLAY_DEMO_GLYPH_H = 8u,
  DISPLAY_DEMO_GLYPH_SCALE = 8u,
  DISPLAY_DEMO_SCALED_GLYPH_W = DISPLAY_DEMO_GLYPH_W * DISPLAY_DEMO_GLYPH_SCALE,
  DISPLAY_DEMO_SCALED_GLYPH_H = DISPLAY_DEMO_GLYPH_H * DISPLAY_DEMO_GLYPH_SCALE,
  DISPLAY_DEMO_LABEL_LEN = 5u,
  DISPLAY_DEMO_VALUE_LEN = 10u,
  DISPLAY_DEMO_TEXT_LEN = DISPLAY_DEMO_LABEL_LEN + DISPLAY_DEMO_VALUE_LEN,
  DISPLAY_DEMO_TEXT_W = DISPLAY_DEMO_TEXT_LEN * DISPLAY_DEMO_GLYPH_W,
  DISPLAY_DEMO_TEXT_H = DISPLAY_DEMO_GLYPH_H,
  DISPLAY_DEMO_PUBLISH_GRANULARITY = 16u,
  DISPLAY_DEMO_TOUCH_RADIUS = 6u,
};

static char g_display_tag[] = "DISPLAY";
static char g_display_init_start[] = "init_start";
static char g_display_panel_init_done[] = "panel_init_done";
static char g_display_mode[] = "display_safe_mode";
static char g_display_ready[] = "gc9a01 ready";
static char g_display_touch_x[] = "touch_x=";
static char g_display_touch_y[] = "touch_y=";

static uint16_t
    g_display_front_buffer[DISPLAY_DEMO_WIDTH * DISPLAY_DEMO_HEIGHT];
static uint16_t g_display_back_buffer[DISPLAY_DEMO_WIDTH * DISPLAY_DEMO_HEIGHT];
static struct display_surface g_display_surface;
static uint32_t g_display_last_published_tick;
static uint32_t g_display_pending_tick;
static uint32_t g_display_ready_flag;
static uint8_t g_display_initialized;
static uint8_t g_display_touch_active;
static uint8_t g_display_debug_suite_enabled;

/* ===========================================================================
 * Complete 8x8 bitmap font for printable ASCII (32 – 127)
 * Modeled after the reference TensorCore font8x8 table.
 * Each character is 8 rows of 8 pixels, MSB-left.
 * =========================================================================*/
static const uint8_t g_font8x8[96][8] = {
    /* 0x20 ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x21 '!' */ {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* 0x22 '"' */ {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x23 '#' */ {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    /* 0x24 '$' */ {0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00},
    /* 0x25 '%' */ {0x00, 0x63, 0x63, 0x08, 0x10, 0x63, 0x63, 0x00},
    /* 0x26 '&' */ {0x1C, 0x36, 0x1C, 0x3B, 0x6E, 0x66, 0x3B, 0x00},
    /* 0x27 '\'' */ {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x28 '(' */ {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    /* 0x29 ')' */ {0x18, 0x30, 0x60, 0x60, 0x60, 0x30, 0x18, 0x00},
    /* 0x2A '*' */ {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    /* 0x2B '+' */ {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    /* 0x2C ',' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x0C},
    /* 0x2D '-' */ {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    /* 0x2E '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* 0x2F '/' */ {0x60, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x06, 0x00},
    /* 0x30 '0' */ {0x3C, 0x66, 0x6E, 0x7E, 0x76, 0x66, 0x3C, 0x00},
    /* 0x31 '1' */ {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* 0x32 '2' */ {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    /* 0x33 '3' */ {0x3C, 0x66, 0x06, 0x3C, 0x06, 0x66, 0x3C, 0x00},
    /* 0x34 '4' */ {0x06, 0x0E, 0x1E, 0x36, 0x7F, 0x06, 0x06, 0x00},
    /* 0x35 '5' */ {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    /* 0x36 '6' */ {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
    /* 0x37 '7' */ {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    /* 0x38 '8' */ {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    /* 0x39 '9' */ {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
    /* 0x3A ':' */ {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00},
    /* 0x3B ';' */ {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x0C, 0x00},
    /* 0x3C '<' */ {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    /* 0x3D '=' */ {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    /* 0x3E '>' */ {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00},
    /* 0x3F '?' */ {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    /* 0x40 '@' */ {0x3C, 0x66, 0x6E, 0x6E, 0x60, 0x3E, 0x00, 0x00},
    /* 0x41 'A' */ {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00},
    /* 0x42 'B' */ {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    /* 0x43 'C' */ {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    /* 0x44 'D' */ {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    /* 0x45 'E' */ {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7E, 0x00},
    /* 0x46 'F' */ {0x7E, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00},
    /* 0x47 'G' */ {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
    /* 0x48 'H' */ {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    /* 0x49 'I' */ {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 0x4A 'J' */ {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    /* 0x4B 'K' */ {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    /* 0x4C 'L' */ {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    /* 0x4D 'M' */ {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    /* 0x4E 'N' */ {0x66, 0x76, 0x7E, 0x7E, 0x7E, 0x6E, 0x66, 0x00},
    /* 0x4F 'O' */ {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 0x50 'P' */ {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    /* 0x51 'Q' */ {0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x0E, 0x07},
    /* 0x52 'R' */ {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00},
    /* 0x53 'S' */ {0x3C, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x3C, 0x00},
    /* 0x54 'T' */ {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* 0x55 'U' */ {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 0x56 'V' */ {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    /* 0x57 'W' */ {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    /* 0x58 'X' */ {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    /* 0x59 'Y' */ {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    /* 0x5A 'Z' */ {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    /* 0x5B '[' */ {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    /* 0x5C '\\' */ {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    /* 0x5D ']' */ {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    /* 0x5E '^' */ {0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x5F '_' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    /* 0x60 '`' */ {0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x61 'a' */ {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
    /* 0x62 'b' */ {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    /* 0x63 'c' */ {0x00, 0x00, 0x3C, 0x60, 0x60, 0x66, 0x3C, 0x00},
    /* 0x64 'd' */ {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    /* 0x65 'e' */ {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    /* 0x66 'f' */ {0x1C, 0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x00},
    /* 0x67 'g' */ {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    /* 0x68 'h' */ {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    /* 0x69 'i' */ {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 0x6A 'j' */ {0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x3C},
    /* 0x6B 'k' */ {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
    /* 0x6C 'l' */ {0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x1C, 0x00},
    /* 0x6D 'm' */ {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    /* 0x6E 'n' */ {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    /* 0x6F 'o' */ {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    /* 0x70 'p' */ {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
    /* 0x71 'q' */ {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
    /* 0x72 'r' */ {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00},
    /* 0x73 's' */ {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    /* 0x74 't' */ {0x30, 0x30, 0x78, 0x30, 0x30, 0x30, 0x1C, 0x00},
    /* 0x75 'u' */ {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
    /* 0x76 'v' */ {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    /* 0x77 'w' */ {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x3E, 0x36, 0x00},
    /* 0x78 'x' */ {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
    /* 0x79 'y' */ {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    /* 0x7A 'z' */ {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    /* 0x7B '{' */ {0x0C, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0C, 0x00},
    /* 0x7C '|' */ {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* 0x7D '}' */ {0x30, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x30, 0x00},
    /* 0x7E '~' */ {0x31, 0x6B, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 0x7F DEL */ {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
};

static const uint8_t *runtime_display_demo_glyph(char ch) {
  if (ch >= 32 && ch <= 127) {
    return g_font8x8[(uint8_t)(ch - 32)];
  }
  return g_font8x8[0];
}

static void runtime_display_demo_clear_backbuffer(uint16_t color) {
  (void)display_surface_fill_rect(&g_display_surface, 0, 0, DISPLAY_DEMO_WIDTH,
                                  DISPLAY_DEMO_HEIGHT, color);
}

/* ---------------------------------------------------------------------------
 * Original font path
 * -------------------------------------------------------------------------*/
static void runtime_display_demo_draw_glyph_8x8(int32_t x, int32_t y, char ch,
                                                uint16_t fg, uint16_t bg) {
  const uint8_t *glyph = runtime_display_demo_glyph(ch);
  uint16_t *back = g_display_surface.back;
  uint16_t stride = g_display_surface.stride;

  if (back == 0) {
    return;
  }

  for (uint16_t row = 0u; row < DISPLAY_DEMO_GLYPH_H; ++row) {
    int32_t py = y + (int32_t)row;
    if (py < 0 || py >= (int32_t)DISPLAY_DEMO_HEIGHT) {
      continue;
    }
    uint8_t bits = glyph[row];
    for (uint16_t col = 0u; col < DISPLAY_DEMO_GLYPH_W; ++col) {
      int32_t px = x + (int32_t)col;
      if (px < 0 || px >= (int32_t)DISPLAY_DEMO_WIDTH) {
        continue;
      }
      back[(uint32_t)py * stride + (uint32_t)px] =
          ((bits & (uint8_t)(0x80u >> col)) != 0u) ? fg : bg;
    }
  }

  (void)display_surface_mark_dirty(&g_display_surface, x, y,
                                   DISPLAY_DEMO_GLYPH_W, DISPLAY_DEMO_GLYPH_H);
}

static void runtime_display_demo_draw_text_8x8(int32_t x, int32_t y,
                                               const char *text, uint16_t fg,
                                               uint16_t bg) {
  int32_t cursor_x = x;

  if (text == 0) {
    return;
  }

  while (*text != '\0') {
    runtime_display_demo_draw_glyph_8x8(cursor_x, y, *text, fg, bg);
    cursor_x += DISPLAY_DEMO_GLYPH_W;
    ++text;
  }
}

/* ---------------------------------------------------------------------------
 * Flush dirty region from front buffer to panel
 * -------------------------------------------------------------------------*/
static void
runtime_display_demo_flush_to_panel(const uint16_t *front_pixels,
                                    const struct display_rect *rect) {
  const uint16_t *rect_pixels;

  if ((front_pixels == 0) || (rect == 0) || (rect->width == 0u) ||
      (rect->height == 0u)) {
    return;
  }

  rect_pixels = &front_pixels[(uint32_t)rect->y * DISPLAY_DEMO_WIDTH + rect->x];
  (void)esp32c3_panel_gc9a01_flush_rect_pixels(rect_pixels, DISPLAY_DEMO_WIDTH,
                                               rect);
}

static void runtime_display_demo_draw_touch_circle(uint16_t center_x,
                                                   uint16_t center_y,
                                                   uint16_t radius,
                                                   uint16_t color) {
  int32_t rr = (int32_t)radius * (int32_t)radius;

  for (int32_t dy = -(int32_t)radius; dy <= (int32_t)radius; ++dy) {
    for (int32_t dx = -(int32_t)radius; dx <= (int32_t)radius; ++dx) {
      int32_t px = (int32_t)center_x + dx;
      int32_t py = (int32_t)center_y + dy;
      if ((dx * dx + dy * dy) > rr) {
        continue;
      }
      if ((px < 0) || (py < 0) || (px >= (int32_t)DISPLAY_DEMO_WIDTH) ||
          (py >= (int32_t)DISPLAY_DEMO_HEIGHT)) {
        continue;
      }
      esp32c3_panel_gc9a01_draw_pixel((uint16_t)px, (uint16_t)py, color);
    }
  }
}

static void runtime_display_demo_probe_touch_canvas(void) {
  esp32c3_panel_gc9a01_fill_screen(DISPLAY_DEMO_BACKGROUND);
}

static void runtime_display_demo_format_tick_text(uint32_t tick_value,
                                                  char *out) {
  static const char label[DISPLAY_DEMO_LABEL_LEN] = {'T', 'I', 'C', 'K', ':'};

  if (out == 0) {
    return;
  }

  for (uint32_t i = 0u; i < DISPLAY_DEMO_LABEL_LEN; ++i) {
    out[i] = label[i];
  }

  for (int32_t i = (int32_t)DISPLAY_DEMO_TEXT_LEN - 1;
       i >= (int32_t)DISPLAY_DEMO_LABEL_LEN; --i) {
    out[i] = (char)('0' + (tick_value % 10u));
    tick_value /= 10u;
  }
  out[DISPLAY_DEMO_TEXT_LEN] = '\0';
}

static void runtime_display_demo_render_frame(uint32_t tick_value) {
  char text[DISPLAY_DEMO_TEXT_LEN + 1u];

  runtime_display_demo_format_tick_text(tick_value, text);
  (void)display_surface_fill_rect(&g_display_surface, 8, 8, DISPLAY_DEMO_TEXT_W,
                                  DISPLAY_DEMO_TEXT_H, DISPLAY_DEMO_BACKGROUND);
  runtime_display_demo_draw_text_8x8(8, 8, text, DISPLAY_DEMO_FOREGROUND,
                                     DISPLAY_DEMO_BACKGROUND);
  runtime_display_demo_flush();
  g_display_last_published_tick = tick_value;
}

/* ===========================================================================
 * DEBUG HELPERS
 * =========================================================================*/

/* 手写点集 A：完全绕过字库和 bit 解析 */
static void runtime_display_demo_draw_manual_a_pixels(uint16_t x, uint16_t y,
                                                      uint16_t color) {
  static const uint8_t pts[][2] = {
      {2, 0}, {1, 1}, {3, 1}, {0, 2}, {4, 2}, {0, 3}, {4, 3}, {0, 4}, {1, 4},
      {2, 4}, {3, 4}, {4, 4}, {0, 5}, {4, 5}, {0, 6}, {4, 6}, {0, 7}, {4, 7},
  };

  uint32_t i;
  for (i = 0u; i < (sizeof(pts) / sizeof(pts[0])); ++i) {
    esp32c3_panel_gc9a01_draw_pixel((uint16_t)(x + pts[i][0]),
                                    (uint16_t)(y + pts[i][1]), color);
  }
}

/* 直接用字库，但逐点 draw_pixel，排除 backbuffer/flush_rect 干扰 */
static void runtime_display_demo_draw_glyph_direct_pixels(uint16_t x,
                                                          uint16_t y, char ch,
                                                          uint16_t fg,
                                                          uint16_t bg) {
  const uint8_t *glyph = runtime_display_demo_glyph(ch);
  uint16_t row;
  uint16_t col;

  for (row = 0u; row < DISPLAY_DEMO_GLYPH_H; ++row) {
    uint8_t bits = glyph[row];
    for (col = 0u; col < DISPLAY_DEMO_GLYPH_W; ++col) {
      uint16_t color = ((bits & (uint8_t)(0x80u >> col)) != 0u) ? fg : bg;
      esp32c3_panel_gc9a01_draw_pixel((uint16_t)(x + col), (uint16_t)(y + row),
                                      color);
    }
  }
}

/* 非对称扫描顺序标定图：8x8，每个像素唯一明暗模式 */
static void runtime_display_demo_make_scan_probe_8x8(uint16_t *buf, uint16_t fg,
                                                     uint16_t bg) {
  uint16_t row;
  uint16_t col;

  for (row = 0u; row < 8u; ++row) {
    for (col = 0u; col < 8u; ++col) {
      /* 做一个非对称图，防止旋转/镜像后仍“看起来差不多” */
      uint8_t on = 0u;

      if (row == 0u) {
        on = (col == 0u || col == 2u || col == 5u || col == 7u) ? 1u : 0u;
      } else if (row == 1u) {
        on = (col == 1u || col == 6u) ? 1u : 0u;
      } else if (row == 2u) {
        on = (col <= 2u) ? 1u : 0u;
      } else if (row == 3u) {
        on = (col == 4u) ? 1u : 0u;
      } else if (row == 4u) {
        on = (col >= 3u) ? 1u : 0u;
      } else if (row == 5u) {
        on = (col == 0u || col == 7u) ? 1u : 0u;
      } else if (row == 6u) {
        on = (col == row) ? 1u : 0u;
      } else {
        on = (col == 3u || col == 4u) ? 1u : 0u;
      }

      buf[(uint32_t)row * 8u + col] = on != 0u ? fg : bg;
    }
  }
}

/* 8x8 逐行编码：用于看 row-major/column-major/倒序 */
static void runtime_display_demo_make_row_code_8x8(uint16_t *buf) {
  static const uint16_t colors[8] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u,
                                     0xF81Fu, 0x07FFu, 0x8410u, 0x0000u};

  uint16_t row;
  uint16_t col;
  for (row = 0u; row < 8u; ++row) {
    for (col = 0u; col < 8u; ++col) {
      buf[(uint32_t)row * 8u + col] = colors[row];
    }
  }
}

/* 8x8 逐列编码 */
static void runtime_display_demo_make_col_code_8x8(uint16_t *buf) {
  static const uint16_t colors[8] = {0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u,
                                     0xF81Fu, 0x07FFu, 0x8410u, 0x0000u};

  uint16_t row;
  uint16_t col;
  for (row = 0u; row < 8u; ++row) {
    for (col = 0u; col < 8u; ++col) {
      buf[(uint32_t)row * 8u + col] = colors[col];
    }
  }
}

/* 只走 flush_rect_pixels 的直刷路径 */
static void runtime_display_demo_flush_test_block(uint16_t x, uint16_t y,
                                                  const uint16_t *pixels,
                                                  uint16_t w, uint16_t h) {
  struct display_rect rect;

  rect.x = x;
  rect.y = y;
  rect.width = w;
  rect.height = h;

  console_log(g_display_tag, "flush_test_block");
  console_log_u32(g_display_tag, "x=", x);
  console_log_u32(g_display_tag, "y=", y);
  console_log_u32(g_display_tag, "w=", w);
  console_log_u32(g_display_tag, "h=", h);
  console_log_u32(g_display_tag, "pixels=", (uint32_t)w * (uint32_t)h);

  (void)esp32c3_panel_gc9a01_flush_rect_pixels(pixels, w, &rect);
}

/* backbuffer 路径上的手写点集 A */
static void runtime_display_demo_draw_manual_a_backbuffer(int32_t x, int32_t y,
                                                          uint16_t fg,
                                                          uint16_t bg) {
  uint16_t row;
  uint16_t col;
  uint16_t *back = g_display_surface.back;
  uint16_t stride = g_display_surface.stride;

  if (back == 0) {
    return;
  }

  for (row = 0u; row < 8u; ++row) {
    for (col = 0u; col < 8u; ++col) {
      uint8_t on = 0u;

      if ((row == 0u && col == 2u) || (row == 1u && (col == 1u || col == 3u)) ||
          (row == 2u && (col == 0u || col == 4u)) ||
          (row == 3u && (col == 0u || col == 4u)) || (row == 4u && col <= 4u) ||
          (row == 5u && (col == 0u || col == 4u)) ||
          (row == 6u && (col == 0u || col == 4u)) ||
          (row == 7u && (col == 0u || col == 4u))) {
        on = 1u;
      }

      back[(uint32_t)(y + row) * stride + (uint32_t)(x + col)] =
          on != 0u ? fg : bg;
    }
  }

  (void)display_surface_mark_dirty(&g_display_surface, x, y, 8u, 8u);
}

static void runtime_display_demo_run_debug_suite(void) {
  static uint16_t probe_asym[64];
  static uint16_t probe_rows[64];
  static uint16_t probe_cols[64];

  console_log(g_display_tag, "debug_suite_begin");

  esp32c3_panel_gc9a01_fill_screen(DISPLAY_DEMO_BACKGROUND);

  /* case 1: 纯 draw_pixel 手写 A */
  console_log(g_display_tag, "case_manual_a_draw_pixel");
  runtime_display_demo_draw_manual_a_pixels(8u, 8u, 0x0000u);

  /* case 2: 纯 draw_pixel 字库 A */
  console_log(g_display_tag, "case_font_a_draw_pixel");
  runtime_display_demo_draw_glyph_direct_pixels(24u, 8u, 'A', 0x0000u, 0xFFFFu);

  /* case 3: 只走 flush_rect_pixels 的非对称 8x8 */
  console_log(g_display_tag, "case_probe_asym_flush");
  runtime_display_demo_make_scan_probe_8x8(probe_asym, 0x0000u, 0xFFFFu);
  runtime_display_demo_flush_test_block(40u, 8u, probe_asym, 8u, 8u);

  /* case 4: 只走 flush_rect_pixels 的逐行编码 */
  console_log(g_display_tag, "case_probe_rows_flush");
  runtime_display_demo_make_row_code_8x8(probe_rows);
  runtime_display_demo_flush_test_block(56u, 8u, probe_rows, 8u, 8u);

  /* case 5: 只走 flush_rect_pixels 的逐列编码 */
  console_log(g_display_tag, "case_probe_cols_flush");
  runtime_display_demo_make_col_code_8x8(probe_cols);
  runtime_display_demo_flush_test_block(72u, 8u, probe_cols, 8u, 8u);

  /* case 6: backbuffer -> publish -> flush_to_panel 路径的手写 A */
  console_log(g_display_tag, "case_manual_a_backbuffer");
  runtime_display_demo_clear_backbuffer(0xFFFFu);
  runtime_display_demo_draw_manual_a_backbuffer(8, 32, 0x0000u, 0xFFFFu);
  runtime_display_demo_flush();

  /* case 7: backbuffer 原字库路径 A */
  console_log(g_display_tag, "case_font_a_backbuffer");
  runtime_display_demo_clear_backbuffer(0xFFFFu);
  runtime_display_demo_draw_glyph_8x8(24, 32, 'A', 0x0000u, 0xFFFFu);
  runtime_display_demo_flush();

  console_log(g_display_tag, "debug_suite_end");
}

/* ===========================================================================
 * PUBLIC API
 * =========================================================================*/

void runtime_display_demo_init(void) {
  reg_write(RTC_CNTL_STORE0_REG, 0xC100u);
  console_log(g_display_tag, g_display_init_start);

  esp32c3_panel_gc9a01_init();
  esp32c3_touch_cst816s_init();

  reg_write(RTC_CNTL_STORE0_REG, 0xC101u);
  console_log(g_display_tag, g_display_panel_init_done);

  runtime_display_demo_probe_touch_canvas();

  display_surface_init(&g_display_surface, DISPLAY_DEMO_WIDTH,
                       DISPLAY_DEMO_HEIGHT, DISPLAY_DEMO_WIDTH,
                       g_display_front_buffer, g_display_back_buffer, 0, 0);

  runtime_display_demo_clear_backbuffer(DISPLAY_DEMO_BACKGROUND);
  for (uint32_t i = 0u; i < (uint32_t)DISPLAY_DEMO_WIDTH * DISPLAY_DEMO_HEIGHT;
       ++i) {
    g_display_front_buffer[i] = DISPLAY_DEMO_BACKGROUND;
  }

  display_surface_clear_damage(&g_display_surface);
  g_display_last_published_tick = 0u;
  g_display_pending_tick = 0u;
  g_display_initialized = 1u;
  g_display_ready_flag = 1u;
  g_display_touch_active = 0u;
  g_display_debug_suite_enabled = 0u;

  reg_write(RTC_CNTL_STORE0_REG, 0xC102u);
  console_log(g_display_tag, g_display_mode);
  console_log(g_display_tag, g_display_ready);

  runtime_display_demo_render_frame(0u);

  if (g_display_debug_suite_enabled != 0u) {
    runtime_display_demo_run_debug_suite();
  }
}

void runtime_display_demo_publish_tick(uint32_t tick_value) {
  if ((tick_value / DISPLAY_DEMO_PUBLISH_GRANULARITY) ==
      (g_display_pending_tick / DISPLAY_DEMO_PUBLISH_GRANULARITY)) {
    return;
  }

  g_display_pending_tick = tick_value;
}

void runtime_display_demo_poll(void) {
  struct esp32c3_touch_cst816s_sample sample;

  if (g_display_initialized == 0u) {
    return;
  }

  if (esp32c3_touch_cst816s_read(&sample) == RUNTIME_SYSCALL_STATUS_OK) {
    if (sample.touching != 0u) {
      if ((g_display_touch_active == 0u) ||
          esp32c3_touch_cst816s_consume_irq_pending()) {
        console_log_u32(g_display_tag, g_display_touch_x, sample.x);
        console_log_u32(g_display_tag, g_display_touch_y, sample.y);
      }
      runtime_display_demo_draw_touch_circle(sample.x, sample.y,
                                             DISPLAY_DEMO_TOUCH_RADIUS,
                                             DISPLAY_DEMO_FOREGROUND);
      g_display_touch_active = 1u;
    } else {
      g_display_touch_active = 0u;
    }
  }

  if ((g_display_pending_tick / DISPLAY_DEMO_PUBLISH_GRANULARITY) !=
      (g_display_last_published_tick / DISPLAY_DEMO_PUBLISH_GRANULARITY)) {
    runtime_display_demo_render_frame(g_display_pending_tick);
  }
}

uint32_t runtime_display_demo_ready(void) { return g_display_ready_flag; }

void runtime_display_demo_draw_text(int32_t x, int32_t y, const char *text,
                                    uint16_t fg, uint16_t bg) {
  runtime_display_demo_draw_text_8x8(x, y, text, fg, bg);
}

void runtime_display_demo_draw_char(int32_t x, int32_t y, char ch, uint16_t fg,
                                    uint16_t bg) {
  runtime_display_demo_draw_glyph_8x8(x, y, ch, fg, bg);
}

void runtime_display_demo_flush(void) {
  struct display_rect rect;
  const uint16_t *front_pixels;

  if (g_display_initialized == 0u) {
    return;
  }

  if (display_surface_publish(&g_display_surface, &rect, &front_pixels) ==
      RUNTIME_SYSCALL_STATUS_OK) {
    runtime_display_demo_flush_to_panel(front_pixels, &rect);
  }
}

struct display_surface *runtime_display_demo_surface(void) {
  return g_display_initialized != 0u ? &g_display_surface : 0;
}
