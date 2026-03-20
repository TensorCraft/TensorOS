#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

enum runtime_display_request {
  RUNTIME_DISPLAY_REQUEST_INFO = 1u,
  RUNTIME_DISPLAY_REQUEST_FLAGS = 2u,
  RUNTIME_DISPLAY_REQUEST_PRESENT_COUNT = 3u
};

enum runtime_display_flags {
  RUNTIME_DISPLAY_FLAG_TEXT_GRID = 1u << 0,
  RUNTIME_DISPLAY_FLAG_KERNEL_LOCAL = 1u << 1
};

struct runtime_display_service {
  uint16_t width;
  uint16_t height;
  uint32_t flags;
  uint32_t present_count;
};

static inline uint32_t runtime_display_pack_geometry(uint16_t width, uint16_t height) {
  return ((uint32_t)width << 16) | (uint32_t)height;
}

static inline uint16_t runtime_display_unpack_width(uint32_t packed_geometry) {
  return (uint16_t)(packed_geometry >> 16);
}

static inline uint16_t runtime_display_unpack_height(uint32_t packed_geometry) {
  return (uint16_t)(packed_geometry & 0xFFFFu);
}

void runtime_display_service_init(struct runtime_display_service *service, uint16_t width,
                                  uint16_t height, uint32_t flags);
void runtime_display_service_mark_present(struct runtime_display_service *service);
int32_t runtime_display_service_request(const struct runtime_display_service *service,
                                        uint32_t request, uint32_t *response);
