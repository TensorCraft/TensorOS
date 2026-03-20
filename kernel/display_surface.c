#include "display_surface.h"

static void display_surface_enter_atomic(const struct display_surface *surface) {
  if ((surface != 0) && (surface->atomic_ops != 0) && (surface->atomic_ops->enter != 0)) {
    surface->atomic_ops->enter(surface->atomic_context);
  }
}

static void display_surface_leave_atomic(const struct display_surface *surface) {
  if ((surface != 0) && (surface->atomic_ops != 0) && (surface->atomic_ops->leave != 0)) {
    surface->atomic_ops->leave(surface->atomic_context);
  }
}

static int display_surface_clip_rect(const struct display_surface *surface, int32_t x, int32_t y,
                                     int32_t width, int32_t height, struct display_rect *rect) {
  int32_t clipped_x = x;
  int32_t clipped_y = y;
  int32_t clipped_w = width;
  int32_t clipped_h = height;

  if ((surface == 0) || (rect == 0) || (width <= 0) || (height <= 0)) {
    return 0;
  }

  if (clipped_x < 0) {
    clipped_w += clipped_x;
    clipped_x = 0;
  }
  if (clipped_y < 0) {
    clipped_h += clipped_y;
    clipped_y = 0;
  }
  if (clipped_x >= (int32_t)surface->width || clipped_y >= (int32_t)surface->height) {
    return 0;
  }
  if ((clipped_x + clipped_w) > (int32_t)surface->width) {
    clipped_w = (int32_t)surface->width - clipped_x;
  }
  if ((clipped_y + clipped_h) > (int32_t)surface->height) {
    clipped_h = (int32_t)surface->height - clipped_y;
  }
  if ((clipped_w <= 0) || (clipped_h <= 0)) {
    return 0;
  }

  rect->x = (uint16_t)clipped_x;
  rect->y = (uint16_t)clipped_y;
  rect->width = (uint16_t)clipped_w;
  rect->height = (uint16_t)clipped_h;
  return 1;
}

void display_surface_init(struct display_surface *surface, uint16_t width, uint16_t height,
                          uint16_t stride, uint16_t *front, uint16_t *back,
                          const struct display_atomic_ops *atomic_ops, void *atomic_context) {
  if (surface == 0) {
    return;
  }

  surface->width = width;
  surface->height = height;
  surface->stride = stride;
  surface->front = front;
  surface->back = back;
  surface->dirty_rect.x = 0u;
  surface->dirty_rect.y = 0u;
  surface->dirty_rect.width = 0u;
  surface->dirty_rect.height = 0u;
  surface->dirty = 0u;
  surface->present_count = 0u;
  surface->atomic_ops = atomic_ops;
  surface->atomic_context = atomic_context;
}

void display_surface_clear_damage(struct display_surface *surface) {
  if (surface == 0) {
    return;
  }

  surface->dirty = 0u;
  surface->dirty_rect.x = 0u;
  surface->dirty_rect.y = 0u;
  surface->dirty_rect.width = 0u;
  surface->dirty_rect.height = 0u;
}

int32_t display_surface_mark_dirty(struct display_surface *surface, int32_t x, int32_t y,
                                   int32_t width, int32_t height) {
  struct display_rect clipped;

  if ((surface == 0) || !display_surface_clip_rect(surface, x, y, width, height, &clipped)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (surface->dirty == 0u) {
    surface->dirty_rect = clipped;
    surface->dirty = 1u;
    return RUNTIME_SYSCALL_STATUS_OK;
  }

  {
    uint16_t x0 = surface->dirty_rect.x < clipped.x ? surface->dirty_rect.x : clipped.x;
    uint16_t y0 = surface->dirty_rect.y < clipped.y ? surface->dirty_rect.y : clipped.y;
    uint16_t x1 = (uint16_t)((surface->dirty_rect.x + surface->dirty_rect.width) >
                                     (clipped.x + clipped.width)
                                 ? (surface->dirty_rect.x + surface->dirty_rect.width)
                                 : (clipped.x + clipped.width));
    uint16_t y1 = (uint16_t)((surface->dirty_rect.y + surface->dirty_rect.height) >
                                     (clipped.y + clipped.height)
                                 ? (surface->dirty_rect.y + surface->dirty_rect.height)
                                 : (clipped.y + clipped.height));

    surface->dirty_rect.x = x0;
    surface->dirty_rect.y = y0;
    surface->dirty_rect.width = (uint16_t)(x1 - x0);
    surface->dirty_rect.height = (uint16_t)(y1 - y0);
  }

  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t display_surface_fill_rect(struct display_surface *surface, int32_t x, int32_t y,
                                  int32_t width, int32_t height, uint16_t color) {
  struct display_rect clipped;

  if ((surface == 0) || (surface->back == 0) ||
      !display_surface_clip_rect(surface, x, y, width, height, &clipped)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint16_t row = 0u; row < clipped.height; ++row) {
    uint32_t offset = (uint32_t)(clipped.y + row) * surface->stride + clipped.x;
    for (uint16_t col = 0u; col < clipped.width; ++col) {
      surface->back[offset + col] = color;
    }
  }

  return display_surface_mark_dirty(surface, clipped.x, clipped.y, clipped.width, clipped.height);
}

int32_t display_surface_blit_rgb565(struct display_surface *surface, int32_t x, int32_t y,
                                    int32_t width, int32_t height, const uint16_t *pixels,
                                    uint16_t source_stride) {
  struct display_rect clipped;

  if ((surface == 0) || (surface->back == 0) || (pixels == 0) || (source_stride == 0u) ||
      !display_surface_clip_rect(surface, x, y, width, height, &clipped)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  {
    int32_t source_x = clipped.x - x;
    int32_t source_y = clipped.y - y;

    for (uint16_t row = 0u; row < clipped.height; ++row) {
      uint32_t dest_offset = (uint32_t)(clipped.y + row) * surface->stride + clipped.x;
      uint32_t src_offset = (uint32_t)(source_y + row) * source_stride + (uint32_t)source_x;
      for (uint16_t col = 0u; col < clipped.width; ++col) {
        surface->back[dest_offset + col] = pixels[src_offset + col];
      }
    }
  }

  return display_surface_mark_dirty(surface, clipped.x, clipped.y, clipped.width, clipped.height);
}

int display_surface_has_pending_present(const struct display_surface *surface,
                                        struct display_rect *rect) {
  if ((surface == 0) || (surface->dirty == 0u)) {
    return 0;
  }

  if (rect != 0) {
    *rect = surface->dirty_rect;
  }

  return 1;
}

int32_t display_surface_publish(struct display_surface *surface, struct display_rect *rect,
                                const uint16_t **front_pixels) {
  uint16_t *previous_front;

  if ((surface == 0) || (surface->front == 0) || (surface->back == 0) || (rect == 0) ||
      (front_pixels == 0) || (surface->dirty == 0u)) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  display_surface_enter_atomic(surface);
  previous_front = surface->front;
  surface->front = surface->back;
  surface->back = previous_front;
  *rect = surface->dirty_rect;
  *front_pixels = surface->front;
  ++surface->present_count;
  display_surface_clear_damage(surface);
  display_surface_leave_atomic(surface);
  return RUNTIME_SYSCALL_STATUS_OK;
}
