#include "display_surface.h"

#include <stdio.h>
#include <stdlib.h>

struct atomic_state {
  uint32_t enter_count;
  uint32_t leave_count;
};

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "display_surface_host_test failed: %s\n", message);
    exit(1);
  }
}

static void host_atomic_enter(void *context) {
  struct atomic_state *state = (struct atomic_state *)context;
  ++state->enter_count;
}

static void host_atomic_leave(void *context) {
  struct atomic_state *state = (struct atomic_state *)context;
  ++state->leave_count;
}

int main(void) {
  uint16_t front[64] = {0};
  uint16_t back[64] = {0};
  struct atomic_state atomic = {0};
  const struct display_atomic_ops atomic_ops = {.enter = host_atomic_enter,
                                                .leave = host_atomic_leave};
  struct display_surface surface;
  struct display_rect rect = {0};
  const uint16_t *presented = 0;
  uint16_t block[4] = {0x1111u, 0x2222u, 0x3333u, 0x4444u};

  display_surface_init(&surface, 8u, 8u, 8u, front, back, &atomic_ops, &atomic);
  expect(display_surface_fill_rect(&surface, 1, 2, 3, 2, 0xF800u) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "fill rect succeeds");
  expect(back[(2u * 8u) + 1u] == 0xF800u, "fill rect writes back buffer");
  expect(display_surface_has_pending_present(&surface, &rect) != 0, "dirty rect is tracked");
  expect(rect.x == 1u && rect.y == 2u && rect.width == 3u && rect.height == 2u,
         "dirty rect preserves filled area");

  expect(display_surface_blit_rgb565(&surface, 4, 4, 2, 2, block, 2u) ==
             RUNTIME_SYSCALL_STATUS_OK,
         "blit succeeds");
  expect(back[(4u * 8u) + 4u] == 0x1111u, "blit writes first pixel");
  expect(back[(5u * 8u) + 5u] == 0x4444u, "blit writes last pixel");
  expect(display_surface_has_pending_present(&surface, &rect) != 0, "dirty rect stays pending");
  expect(rect.x == 1u && rect.y == 2u && rect.width == 5u && rect.height == 4u,
         "dirty rect unions multiple writes");

  expect(display_surface_publish(&surface, &rect, &presented) == RUNTIME_SYSCALL_STATUS_OK,
         "publish succeeds");
  expect(presented == back, "publish swaps front and back buffers");
  expect(surface.present_count == 1u, "publish increments present count");
  expect(surface.dirty == 0u, "publish clears pending damage");
  expect(atomic.enter_count == 1u && atomic.leave_count == 1u,
         "publish uses atomic hooks");

  expect(display_surface_publish(&surface, &rect, &presented) == RUNTIME_SYSCALL_STATUS_ENOENT,
         "publish without new damage reports enoent");

  puts("display surface host checks passed");
  return 0;
}
