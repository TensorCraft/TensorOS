#pragma once

#include "display_surface.h"

#include <stdint.h>

void runtime_display_demo_init(void);
void runtime_display_demo_poll(void);
void runtime_display_demo_publish_tick(uint32_t tick_value);
uint32_t runtime_display_demo_ready(void);

/* Public text rendering API */
void runtime_display_demo_draw_text(int32_t x, int32_t y, const char *text,
                                    uint16_t fg, uint16_t bg);
void runtime_display_demo_draw_char(int32_t x, int32_t y, char ch,
                                    uint16_t fg, uint16_t bg);
void runtime_display_demo_flush(void);
struct display_surface *runtime_display_demo_surface(void);
