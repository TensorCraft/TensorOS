#pragma once

#include "runtime_ui.h"

#include <stdint.h>

enum runtime_input_request {
  RUNTIME_INPUT_REQUEST_FLAGS = 1u,
  RUNTIME_INPUT_REQUEST_DEPTH = 2u
};

enum runtime_input_flags {
  RUNTIME_INPUT_FLAG_KEYBOARD = 1u << 0,
  RUNTIME_INPUT_FLAG_POINTER = 1u << 1,
  RUNTIME_INPUT_FLAG_FOCUS = 1u << 2
};

struct runtime_input_service {
  uint32_t flags;
  struct runtime_ui_event_queue queue;
};

void runtime_input_service_init(struct runtime_input_service *service, uint32_t flags);
int32_t runtime_input_service_request(const struct runtime_input_service *service, uint32_t request,
                                      uint32_t *response);
int32_t runtime_input_push_key(struct runtime_input_service *service, uint32_t target_pid,
                               uint32_t tick, uint32_t keycode, uint32_t pressed);
int32_t runtime_input_push_pointer_move(struct runtime_input_service *service, uint32_t target_pid,
                                        uint32_t tick, uint32_t x, uint32_t y);
int32_t runtime_input_push_pointer_button(struct runtime_input_service *service, uint32_t target_pid,
                                          uint32_t tick, uint32_t button,
                                          uint32_t pressed);
int32_t runtime_input_push_focus(struct runtime_input_service *service, uint32_t target_pid,
                                 uint32_t tick, uint32_t focused);
int32_t runtime_input_pop(struct runtime_input_service *service, struct runtime_ui_event *event);
