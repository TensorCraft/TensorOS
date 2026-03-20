#include "runtime.h"

#define KERNEL_MUTEX_MAGIC 0x4D555458u

struct kernel_mutex {
  uint32_t magic;
  process_id_t owner_pid;
  struct kernel_wait_queue *wait_queue;
};

static int kernel_mutex_is_valid(const struct kernel_mutex *mutex) {
  return (mutex != 0) && (mutex->magic == KERNEL_MUTEX_MAGIC) && (mutex->wait_queue != 0);
}

struct kernel_mutex *kernel_mutex_create(void) {
  struct kernel_mutex *mutex =
      (struct kernel_mutex *)kmem_alloc((uint32_t)sizeof(struct kernel_mutex));
  if (mutex == 0) {
    return 0;
  }

  mutex->wait_queue = kernel_wait_queue_create();
  if (mutex->wait_queue == 0) {
    kmem_free(mutex);
    return 0;
  }

  mutex->magic = KERNEL_MUTEX_MAGIC;
  mutex->owner_pid = 0u;
  return mutex;
}

int kernel_mutex_destroy(struct kernel_mutex *mutex) {
  if (!kernel_mutex_is_valid(mutex) || (mutex->owner_pid != 0u) ||
      !kernel_wait_queue_destroy(mutex->wait_queue)) {
    return 0;
  }

  mutex->magic = 0u;
  mutex->wait_queue = 0;
  kmem_free(mutex);
  return 1;
}

int kernel_mutex_try_lock(struct kernel_mutex *mutex) {
  process_id_t current_pid;

  if (!kernel_mutex_is_valid(mutex)) {
    return 0;
  }

  current_pid = process_current_pid();
  if ((current_pid == 0u) || (mutex->owner_pid != 0u)) {
    return 0;
  }

  mutex->owner_pid = current_pid;
  return 1;
}

void kernel_mutex_lock(struct kernel_mutex *mutex) {
  if (!kernel_mutex_is_valid(mutex)) {
    return;
  }

  while (!kernel_mutex_try_lock(mutex)) {
    kernel_wait_queue_wait(mutex->wait_queue);
  }
}

int kernel_mutex_unlock(struct kernel_mutex *mutex) {
  process_id_t current_pid;

  if (!kernel_mutex_is_valid(mutex)) {
    return 0;
  }

  current_pid = process_current_pid();
  if ((current_pid == 0u) || (mutex->owner_pid != current_pid)) {
    return 0;
  }

  mutex->owner_pid = 0u;
  if (kernel_wait_queue_waiter_count(mutex->wait_queue) != 0u) {
    (void)kernel_wait_queue_wake_one(mutex->wait_queue);
  }
  return 1;
}

process_id_t kernel_mutex_owner_pid(const struct kernel_mutex *mutex) {
  return kernel_mutex_is_valid(mutex) ? mutex->owner_pid : 0u;
}

uint32_t kernel_mutex_waiter_count(const struct kernel_mutex *mutex) {
  return kernel_mutex_is_valid(mutex)
             ? kernel_wait_queue_waiter_count(mutex->wait_queue)
             : 0u;
}
