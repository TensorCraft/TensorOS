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
  DISPLAY_DEMO_PUBLISH_GRANULARITY = 16u,
};

static char g_display_tag[] = "DISPLAY";
static char g_display_init_start[] = "init_start";
static char g_display_panel_init_done[] = "panel_init_done";
static char g_display_mode[] = "display_safe_mode";
static char g_display_ready[] = "gc9a01 ready";
static char g_display_google_g_upper[] = "G";
static char g_display_google_o_first[] = "o";
static char g_display_google_o_second[] = "o";
static char g_display_google_g_lower[] = "g";
static char g_display_google_l[] = "l";
static char g_display_google_e[] = "e";
#include "runtime_display_demo_font8x8.inc"

static uint16_t
    g_display_front_buffer[DISPLAY_DEMO_WIDTH * DISPLAY_DEMO_HEIGHT];
static uint16_t g_display_back_buffer[DISPLAY_DEMO_WIDTH * DISPLAY_DEMO_HEIGHT];
static uint16_t g_display_glyph_buffer[DISPLAY_DEMO_GLYPH_W * DISPLAY_DEMO_GLYPH_H];
static struct display_surface g_display_surface;
static uint32_t g_display_last_published_tick;
static uint32_t g_display_ready_flag;
static uint8_t g_display_initialized;
static uint16_t g_google_blue = 0x001Fu;
static uint16_t g_google_red = 0xF800u;
static uint16_t g_google_yellow = 0xFFE0u;
static uint16_t g_google_green = 0x07E0u;

static void runtime_display_demo_clear_backbuffer(uint16_t color) {
  (void)display_surface_fill_rect(&g_display_surface, 0, 0, DISPLAY_DEMO_WIDTH,
                                  DISPLAY_DEMO_HEIGHT, color);
}

static const uint8_t *runtime_display_demo_lookup_glyph(char ch) {
  const uint8_t uch = (uint8_t)ch;

  if ((uch < 32u) || (uch > 127u)) {
    return 0;
  }

  return g_display_demo_font8x8[uch - 32u];
}

static void runtime_display_demo_make_glyph_pixels(const uint8_t *bitmap,
                                                   uint16_t fg, uint16_t bg,
                                                   uint16_t *out_pixels) {
  if (out_pixels == 0) {
    return;
  }

  for (uint16_t row = 0u; row < DISPLAY_DEMO_GLYPH_H; ++row) {
    const uint8_t bits = bitmap != 0 ? bitmap[row] : 0u;
    for (uint16_t col = 0u; col < DISPLAY_DEMO_GLYPH_W; ++col) {
      out_pixels[(uint32_t)row * DISPLAY_DEMO_GLYPH_W + col] =
          (bits & (uint8_t)(0x80u >> col)) != 0u ? fg : bg;
    }
  }
}

static void runtime_display_demo_draw_fast_char_at(uint16_t x, uint16_t y,
                                                   char ch, uint16_t fg,
                                                   uint16_t bg) {
  const uint8_t *bitmap = runtime_display_demo_lookup_glyph(ch);
  runtime_display_demo_make_glyph_pixels(bitmap, fg, bg, g_display_glyph_buffer);
  (void)display_surface_blit_rgb565(&g_display_surface, x, y, DISPLAY_DEMO_GLYPH_W,
                                    DISPLAY_DEMO_GLYPH_H, g_display_glyph_buffer,
                                    DISPLAY_DEMO_GLYPH_W);
}

static void runtime_display_demo_draw_fast_text_at(uint16_t x, uint16_t y,
                                                   const char *text,
                                                   uint16_t fg,
                                                   uint16_t bg) {
  uint16_t cursor_x = x;

  if (text == 0) {
    return;
  }

  while (*text != 0) {
    runtime_display_demo_draw_fast_char_at(cursor_x, y, *text, fg, bg);
    cursor_x = (uint16_t)(cursor_x + DISPLAY_DEMO_GLYPH_W);
    ++text;
  }
}

static void runtime_display_demo_blit_scaled_glyph(uint16_t x, uint16_t y,
                                                   const uint16_t *pixels,
                                                   uint16_t scale) {
  for (uint16_t row = 0u; row < DISPLAY_DEMO_GLYPH_H; ++row) {
    for (uint16_t y_repeat = 0u; y_repeat < scale; ++y_repeat) {
      for (uint16_t col = 0u; col < DISPLAY_DEMO_GLYPH_W; ++col) {
        const uint16_t pixel =
            pixels[(uint32_t)row * DISPLAY_DEMO_GLYPH_W + col];
        for (uint16_t x_repeat = 0u; x_repeat < scale; ++x_repeat) {
          (void)display_surface_fill_rect(
              &g_display_surface, (int32_t)(x + col * scale + x_repeat),
              (int32_t)(y + row * scale + y_repeat), 1, 1, pixel);
        }
      }
    }
  }
}

static void runtime_display_demo_draw_google_letter(uint16_t x, uint16_t y,
                                                    const char *text,
                                                    uint16_t color,
                                                    uint16_t scale) {
  const uint8_t *bitmap;

  if ((text == 0) || (text[0] == 0)) {
    return;
  }

  bitmap = runtime_display_demo_lookup_glyph(text[0]);
  runtime_display_demo_make_glyph_pixels(bitmap, color, DISPLAY_DEMO_BACKGROUND,
                                         g_display_glyph_buffer);
  runtime_display_demo_blit_scaled_glyph(x, y, g_display_glyph_buffer, scale);
}

static void runtime_display_demo_draw_google_wordmark(void) {
  const uint16_t glyph_scale = 3u;
  const uint16_t glyph_width = DISPLAY_DEMO_GLYPH_W * glyph_scale;
  const uint16_t glyph_height = DISPLAY_DEMO_GLYPH_H * glyph_scale;
  const uint16_t text_width = glyph_width * 6u;
  const uint16_t origin_x = (uint16_t)((DISPLAY_DEMO_WIDTH - text_width) / 2u);
  const uint16_t origin_y = (uint16_t)((DISPLAY_DEMO_HEIGHT - glyph_height) / 2u);

  esp32c3_panel_gc9a01_set_madctl(0x08u);
  runtime_display_demo_clear_backbuffer(DISPLAY_DEMO_BACKGROUND);
  runtime_display_demo_draw_google_letter(origin_x, origin_y,
                                          g_display_google_g_upper,
                                          g_google_blue, glyph_scale);
  runtime_display_demo_draw_google_letter((uint16_t)(origin_x + glyph_width),
                                          origin_y, g_display_google_o_first,
                                          g_google_red, glyph_scale);
  runtime_display_demo_draw_google_letter(
      (uint16_t)(origin_x + glyph_width * 2u), origin_y,
      g_display_google_o_second, g_google_yellow, glyph_scale);
  runtime_display_demo_draw_google_letter(
      (uint16_t)(origin_x + glyph_width * 3u), origin_y,
      g_display_google_g_lower, g_google_blue, glyph_scale);
  runtime_display_demo_draw_google_letter((uint16_t)(origin_x + glyph_width * 4u),
                                          origin_y, g_display_google_l,
                                          g_google_green, glyph_scale);
  runtime_display_demo_draw_google_letter((uint16_t)(origin_x + glyph_width * 5u),
                                          origin_y, g_display_google_e,
                                          g_google_red, glyph_scale);
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

static void runtime_display_demo_render_frame(uint32_t tick_value) {
  g_display_last_published_tick = tick_value;
}

static void runtime_display_demo_present_pending(void) {
  struct display_rect rect;
  const uint16_t *front_pixels;

  if (display_surface_publish(&g_display_surface, &rect, &front_pixels) ==
      RUNTIME_SYSCALL_STATUS_OK) {
    runtime_display_demo_flush_to_panel(front_pixels, &rect);
  }
}

/* ===========================================================================
 * PUBLIC API
 * =========================================================================*/

void runtime_display_demo_init(void) {
  if (g_display_initialized != 0u) {
    return;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xC100u);
  console_log(g_display_tag, g_display_init_start);

  esp32c3_panel_gc9a01_init();
  esp32c3_touch_cst816s_init();

  reg_write(RTC_CNTL_STORE0_REG, 0xC101u);
  console_log(g_display_tag, g_display_panel_init_done);

  esp32c3_panel_gc9a01_fill_screen(DISPLAY_DEMO_BACKGROUND);

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
  g_display_initialized = 1u;
  g_display_ready_flag = 1u;

  reg_write(RTC_CNTL_STORE0_REG, 0xC102u);
  console_log(g_display_tag, g_display_mode);
  console_log(g_display_tag, g_display_ready);

  runtime_display_demo_draw_google_wordmark();
  runtime_display_demo_present_pending();
  runtime_display_demo_render_frame(0u);
}

void runtime_display_demo_publish_tick(uint32_t tick_value) {
  if (g_display_initialized == 0u) {
    return;
  }

  if ((tick_value - g_display_last_published_tick) <
      DISPLAY_DEMO_PUBLISH_GRANULARITY) {
    return;
  }

  runtime_display_demo_render_frame(tick_value);
}

void runtime_display_demo_poll(void) {
  if (g_display_initialized == 0u) {
    return;
  }
}

uint32_t runtime_display_demo_ready(void) { return g_display_ready_flag; }

void runtime_display_demo_clear(uint16_t color) {
  if (g_display_initialized == 0u) {
    return;
  }

  (void)display_surface_fill_rect(&g_display_surface, 0, 0, DISPLAY_DEMO_WIDTH,
                                  DISPLAY_DEMO_HEIGHT, color);
}

int32_t runtime_display_demo_fill_rect(int32_t x, int32_t y, int32_t width,
                                       int32_t height, uint16_t color) {
  if (g_display_initialized == 0u) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  return display_surface_fill_rect(&g_display_surface, x, y, width, height,
                                   color);
}

int32_t runtime_display_demo_blit_rgb565(int32_t x, int32_t y, int32_t width,
                                         int32_t height,
                                         const uint16_t *pixels,
                                         uint16_t source_stride) {
  if (g_display_initialized == 0u) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  return display_surface_blit_rgb565(&g_display_surface, x, y, width, height,
                                     pixels, source_stride);
}

void runtime_display_demo_draw_text(int32_t x, int32_t y, const char *text,
                                    uint16_t fg, uint16_t bg) {
  if ((x < 0) || (y < 0)) {
    return;
  }

  runtime_display_demo_draw_fast_text_at((uint16_t)x, (uint16_t)y, text, fg,
                                         bg);
}

void runtime_display_demo_draw_char(int32_t x, int32_t y, char ch, uint16_t fg,
                                    uint16_t bg) {
  if ((x < 0) || (y < 0)) {
    return;
  }

  runtime_display_demo_draw_fast_char_at((uint16_t)x, (uint16_t)y, ch, fg, bg);
}

int runtime_display_demo_has_pending_present(struct display_rect *rect) {
  if (g_display_initialized == 0u) {
    return 0;
  }

  return display_surface_has_pending_present(&g_display_surface, rect);
}

int32_t runtime_display_demo_present(void) {
  struct display_rect rect;
  const uint16_t *front_pixels;

  if (g_display_initialized == 0u) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (display_surface_publish(&g_display_surface, &rect, &front_pixels) !=
      RUNTIME_SYSCALL_STATUS_OK) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  runtime_display_demo_flush_to_panel(front_pixels, &rect);
  return RUNTIME_SYSCALL_STATUS_OK;
}

void runtime_display_demo_flush(void) {
  (void)runtime_display_demo_present();
}

struct display_surface *runtime_display_demo_surface(void) {
  return g_display_initialized != 0u ? &g_display_surface : 0;
}
