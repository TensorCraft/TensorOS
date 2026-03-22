#pragma once

#include "display_surface.h"

#include <stdint.h>

#define ESP32C3_PANEL_GC9A01_WIDTH 240u
#define ESP32C3_PANEL_GC9A01_HEIGHT 240u

void esp32c3_panel_gc9a01_init(void);
void esp32c3_panel_gc9a01_backlight_set(uint8_t percent);
void esp32c3_panel_gc9a01_begin_batch(void);
void esp32c3_panel_gc9a01_end_batch(void);
void esp32c3_panel_gc9a01_fill_screen(uint16_t color);
void esp32c3_panel_gc9a01_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void esp32c3_panel_gc9a01_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                    uint16_t color);
void esp32c3_panel_gc9a01_set_madctl(uint8_t value);
int32_t esp32c3_panel_gc9a01_flush_rect(const uint16_t *pixels, uint16_t stride,
                                        const struct display_rect *rect);
int32_t esp32c3_panel_gc9a01_flush_rect_pixels(const uint16_t *pixels, uint16_t stride,
                                                const struct display_rect *rect);
int32_t esp32c3_panel_gc9a01_flush_rect_stream(const uint16_t *pixels, uint16_t stride,
                                               const struct display_rect *rect);
