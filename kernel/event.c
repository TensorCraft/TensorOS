#include "runtime.h"

#define KERNEL_EVENT_MAGIC 0x45564E54u

struct kernel_event {
  uint32_t magic;
  struct kernel_wait_queue *wait_queue;
};

static int kernel_event_is_valid(const struct kernel_event *event) {
  return (event != 0) && (event->magic == KERNEL_EVENT_MAGIC);
}

struct kernel_event *kernel_event_create(void) {
  struct kernel_event *event = (struct kernel_event *)kmem_alloc((uint32_t)sizeof(struct kernel_event));
  if (event == 0) {
    return 0;
  }

  event->wait_queue = kernel_wait_queue_create();
  if (event->wait_queue == 0) {
    kmem_free(event);
    return 0;
  }

  event->magic = KERNEL_EVENT_MAGIC;
  return event;
}

int kernel_event_destroy(struct kernel_event *event) {
  if (!kernel_event_is_valid(event) || !kernel_wait_queue_destroy(event->wait_queue)) {
    return 0;
  }

  event->magic = 0u;
  event->wait_queue = 0;
  kmem_free(event);
  return 1;
}

void kernel_event_wait(struct kernel_event *event) {
  if (!kernel_event_is_valid(event)) {
    return;
  }

  kernel_wait_queue_wait(event->wait_queue);
}

uint32_t kernel_event_signal(struct kernel_event *event) {
  if (!kernel_event_is_valid(event)) {
    return 0u;
  }

  return kernel_wait_queue_wake_all(event->wait_queue);
}

uint32_t kernel_event_waiter_count(const struct kernel_event *event) {
  return kernel_event_is_valid(event) ? kernel_wait_queue_waiter_count(event->wait_queue) : 0u;
}
