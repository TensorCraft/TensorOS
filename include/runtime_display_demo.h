#pragma once

#include "display_surface.h"

#include <stdint.h>

void runtime_display_demo_init(void);
void runtime_display_demo_poll(void);
void runtime_display_demo_publish_tick(uint32_t tick_value);
uint32_t runtime_display_demo_ready(void);

/* Backbuffer drawing API for future UI work. These calls only mutate the
 * software surface and mark dirty rectangles; call present/flush to commit. */
void runtime_display_demo_clear(uint16_t color);
int32_t runtime_display_demo_fill_rect(int32_t x, int32_t y, int32_t width,
                                       int32_t height, uint16_t color);
int32_t runtime_display_demo_blit_rgb565(int32_t x, int32_t y, int32_t width,
                                         int32_t height,
                                         const uint16_t *pixels,
                                         uint16_t source_stride);
void runtime_display_demo_draw_text(int32_t x, int32_t y, const char *text,
                                    uint16_t fg, uint16_t bg);
void runtime_display_demo_draw_char(int32_t x, int32_t y, char ch,
                                    uint16_t fg, uint16_t bg);
int runtime_display_demo_has_pending_present(struct display_rect *rect);
int32_t runtime_display_demo_present(void);
void runtime_display_demo_flush(void);
struct display_surface *runtime_display_demo_surface(void);
