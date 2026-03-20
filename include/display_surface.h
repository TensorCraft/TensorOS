#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

struct display_rect {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
};

struct display_atomic_ops {
  void (*enter)(void *context);
  void (*leave)(void *context);
};

struct display_surface {
  uint16_t width;
  uint16_t height;
  uint16_t stride;
  uint16_t *front;
  uint16_t *back;
  struct display_rect dirty_rect;
  uint8_t dirty;
  uint32_t present_count;
  const struct display_atomic_ops *atomic_ops;
  void *atomic_context;
};

void display_surface_init(struct display_surface *surface, uint16_t width, uint16_t height,
                          uint16_t stride, uint16_t *front, uint16_t *back,
                          const struct display_atomic_ops *atomic_ops, void *atomic_context);
void display_surface_clear_damage(struct display_surface *surface);
int32_t display_surface_mark_dirty(struct display_surface *surface, int32_t x, int32_t y,
                                   int32_t width, int32_t height);
int32_t display_surface_fill_rect(struct display_surface *surface, int32_t x, int32_t y,
                                  int32_t width, int32_t height, uint16_t color);
int32_t display_surface_blit_rgb565(struct display_surface *surface, int32_t x, int32_t y,
                                    int32_t width, int32_t height, const uint16_t *pixels,
                                    uint16_t source_stride);
int display_surface_has_pending_present(const struct display_surface *surface,
                                        struct display_rect *rect);
int32_t display_surface_publish(struct display_surface *surface, struct display_rect *rect,
                                const uint16_t **front_pixels);
