#include "runtime.h"

#define KERNEL_SEMAPHORE_MAGIC 0x53454D41u

struct kernel_semaphore {
  uint32_t magic;
  uint32_t count;
  uint32_t max_count;
  struct kernel_wait_queue *wait_queue;
};

static int kernel_semaphore_is_valid(const struct kernel_semaphore *semaphore) {
  return (semaphore != 0) && (semaphore->magic == KERNEL_SEMAPHORE_MAGIC) &&
         (semaphore->wait_queue != 0);
}

struct kernel_semaphore *kernel_semaphore_create(uint32_t initial_count, uint32_t max_count) {
  if ((max_count == 0u) || (initial_count > max_count)) {
    return 0;
  }

  struct kernel_semaphore *semaphore =
      (struct kernel_semaphore *)kmem_alloc((uint32_t)sizeof(struct kernel_semaphore));
  if (semaphore == 0) {
    return 0;
  }

  semaphore->wait_queue = kernel_wait_queue_create();
  if (semaphore->wait_queue == 0) {
    kmem_free(semaphore);
    return 0;
  }

  semaphore->magic = KERNEL_SEMAPHORE_MAGIC;
  semaphore->count = initial_count;
  semaphore->max_count = max_count;
  return semaphore;
}

int kernel_semaphore_destroy(struct kernel_semaphore *semaphore) {
  if (!kernel_semaphore_is_valid(semaphore) ||
      !kernel_wait_queue_destroy(semaphore->wait_queue)) {
    return 0;
  }

  semaphore->magic = 0u;
  semaphore->wait_queue = 0;
  kmem_free(semaphore);
  return 1;
}

int kernel_semaphore_try_acquire(struct kernel_semaphore *semaphore) {
  if (!kernel_semaphore_is_valid(semaphore) || (semaphore->count == 0u)) {
    return 0;
  }

  --semaphore->count;
  return 1;
}

void kernel_semaphore_acquire(struct kernel_semaphore *semaphore) {
  if (!kernel_semaphore_is_valid(semaphore)) {
    return;
  }

  while (!kernel_semaphore_try_acquire(semaphore)) {
    kernel_wait_queue_wait(semaphore->wait_queue);
  }
}

int kernel_semaphore_release(struct kernel_semaphore *semaphore) {
  if (!kernel_semaphore_is_valid(semaphore) || (semaphore->count == semaphore->max_count)) {
    return 0;
  }

  ++semaphore->count;
  if (kernel_wait_queue_waiter_count(semaphore->wait_queue) != 0u) {
    (void)kernel_wait_queue_wake_one(semaphore->wait_queue);
  }
  return 1;
}

uint32_t kernel_semaphore_count(const struct kernel_semaphore *semaphore) {
  return kernel_semaphore_is_valid(semaphore) ? semaphore->count : 0u;
}

uint32_t kernel_semaphore_waiter_count(const struct kernel_semaphore *semaphore) {
  return kernel_semaphore_is_valid(semaphore)
             ? kernel_wait_queue_waiter_count(semaphore->wait_queue)
             : 0u;
}
