#include "runtime_input.h"

static int32_t runtime_input_push(struct runtime_input_service *service, uint32_t type,
                                  uint32_t target_pid, uint32_t tick, uint32_t arg0,
                                  uint32_t arg1, uint32_t arg2) {
  struct runtime_ui_event event;

  if (service == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  event.type = type;
  event.target_pid = target_pid;
  event.tick = tick;
  event.arg0 = arg0;
  event.arg1 = arg1;
  event.arg2 = arg2;
  return runtime_ui_event_push(&service->queue, &event);
}

void runtime_input_service_init(struct runtime_input_service *service, uint32_t flags) {
  if (service == 0) {
    return;
  }

  service->flags = flags;
  runtime_ui_event_queue_init(&service->queue);
}

int32_t runtime_input_service_request(const struct runtime_input_service *service, uint32_t request,
                                      uint32_t *response) {
  if ((service == 0) || (response == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  switch (request) {
    case RUNTIME_INPUT_REQUEST_FLAGS:
      *response = service->flags;
      return RUNTIME_SYSCALL_STATUS_OK;
    case RUNTIME_INPUT_REQUEST_DEPTH:
      *response = service->queue.count;
      return RUNTIME_SYSCALL_STATUS_OK;
    default:
      return RUNTIME_SYSCALL_STATUS_ENOSYS;
  }
}

int32_t runtime_input_push_key(struct runtime_input_service *service, uint32_t target_pid,
                               uint32_t tick, uint32_t keycode, uint32_t pressed) {
  return runtime_input_push(service, RUNTIME_UI_EVENT_INPUT_KEY, target_pid, tick, keycode, pressed,
                            0u);
}

int32_t runtime_input_push_pointer_move(struct runtime_input_service *service, uint32_t target_pid,
                                        uint32_t tick, uint32_t x, uint32_t y) {
  return runtime_input_push(service, RUNTIME_UI_EVENT_INPUT_POINTER_MOVE, target_pid, tick, x, y,
                            0u);
}

int32_t runtime_input_push_pointer_button(struct runtime_input_service *service, uint32_t target_pid,
                                          uint32_t tick, uint32_t button,
                                          uint32_t pressed) {
  return runtime_input_push(service, RUNTIME_UI_EVENT_INPUT_POINTER_BUTTON, target_pid, tick,
                            button, pressed, 0u);
}

int32_t runtime_input_push_focus(struct runtime_input_service *service, uint32_t target_pid,
                                 uint32_t tick, uint32_t focused) {
  return runtime_input_push(service, RUNTIME_UI_EVENT_FOCUS, target_pid, tick, focused, 0u, 0u);
}

int32_t runtime_input_pop(struct runtime_input_service *service, struct runtime_ui_event *event) {
  if (service == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  return runtime_ui_event_pop(&service->queue, event);
}
