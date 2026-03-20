#include "runtime.h"

#define KERNEL_WAIT_QUEUE_MAGIC 0x57515545u

struct kernel_wait_queue {
  uint32_t magic;
  uint32_t channel;
  uint32_t waiter_count;
};

static uint32_t g_next_wait_queue_channel = 0x1000u;

static int kernel_wait_queue_is_valid(const struct kernel_wait_queue *wait_queue) {
  return (wait_queue != 0) && (wait_queue->magic == KERNEL_WAIT_QUEUE_MAGIC);
}

struct kernel_wait_queue *kernel_wait_queue_create(void) {
  struct kernel_wait_queue *wait_queue =
      (struct kernel_wait_queue *)kmem_alloc((uint32_t)sizeof(struct kernel_wait_queue));
  if (wait_queue == 0) {
    return 0;
  }

  wait_queue->magic = KERNEL_WAIT_QUEUE_MAGIC;
  wait_queue->channel = g_next_wait_queue_channel++;
  wait_queue->waiter_count = 0u;
  return wait_queue;
}

int kernel_wait_queue_destroy(struct kernel_wait_queue *wait_queue) {
  if (!kernel_wait_queue_is_valid(wait_queue) || (wait_queue->waiter_count != 0u)) {
    return 0;
  }

  wait_queue->magic = 0u;
  kmem_free(wait_queue);
  return 1;
}

void kernel_wait_queue_wait(struct kernel_wait_queue *wait_queue) {
  if (!kernel_wait_queue_is_valid(wait_queue)) {
    return;
  }

  ++wait_queue->waiter_count;
  process_wait_channel(wait_queue->channel);
  if (wait_queue->waiter_count != 0u) {
    --wait_queue->waiter_count;
  }
}

uint32_t kernel_wait_queue_wake_one(struct kernel_wait_queue *wait_queue) {
  if (!kernel_wait_queue_is_valid(wait_queue)) {
    return 0u;
  }

  return process_wake_first_channel(wait_queue->channel);
}

uint32_t kernel_wait_queue_wake_all(struct kernel_wait_queue *wait_queue) {
  if (!kernel_wait_queue_is_valid(wait_queue)) {
    return 0u;
  }

  return process_wake_channel(wait_queue->channel);
}

uint32_t kernel_wait_queue_waiter_count(const struct kernel_wait_queue *wait_queue) {
  return kernel_wait_queue_is_valid(wait_queue) ? wait_queue->waiter_count : 0u;
}
