#include "runtime.h"

#define KERNEL_MAILBOX_MAGIC 0x4D41494Cu

struct kernel_mailbox {
  uint32_t magic;
  uint32_t message;
  uint32_t has_message;
  struct kernel_wait_queue *sender_wait_queue;
  struct kernel_wait_queue *receiver_wait_queue;
};

static int kernel_mailbox_is_valid(const struct kernel_mailbox *mailbox) {
  return (mailbox != 0) && (mailbox->magic == KERNEL_MAILBOX_MAGIC) &&
         (mailbox->sender_wait_queue != 0) && (mailbox->receiver_wait_queue != 0);
}

struct kernel_mailbox *kernel_mailbox_create(void) {
  struct kernel_mailbox *mailbox =
      (struct kernel_mailbox *)kmem_alloc((uint32_t)sizeof(struct kernel_mailbox));
  if (mailbox == 0) {
    return 0;
  }

  mailbox->sender_wait_queue = kernel_wait_queue_create();
  if (mailbox->sender_wait_queue == 0) {
    kmem_free(mailbox);
    return 0;
  }

  mailbox->receiver_wait_queue = kernel_wait_queue_create();
  if (mailbox->receiver_wait_queue == 0) {
    (void)kernel_wait_queue_destroy(mailbox->sender_wait_queue);
    kmem_free(mailbox);
    return 0;
  }

  mailbox->magic = KERNEL_MAILBOX_MAGIC;
  mailbox->message = 0u;
  mailbox->has_message = 0u;
  return mailbox;
}

int kernel_mailbox_destroy(struct kernel_mailbox *mailbox) {
  if (!kernel_mailbox_is_valid(mailbox) || (mailbox->has_message != 0u) ||
      !kernel_wait_queue_destroy(mailbox->sender_wait_queue) ||
      !kernel_wait_queue_destroy(mailbox->receiver_wait_queue)) {
    return 0;
  }

  mailbox->magic = 0u;
  mailbox->sender_wait_queue = 0;
  mailbox->receiver_wait_queue = 0;
  kmem_free(mailbox);
  return 1;
}

int kernel_mailbox_try_send(struct kernel_mailbox *mailbox, uint32_t message) {
  if (!kernel_mailbox_is_valid(mailbox) || (mailbox->has_message != 0u)) {
    return 0;
  }

  mailbox->message = message;
  mailbox->has_message = 1u;
  if (kernel_wait_queue_waiter_count(mailbox->receiver_wait_queue) != 0u) {
    (void)kernel_wait_queue_wake_one(mailbox->receiver_wait_queue);
  }
  return 1;
}

void kernel_mailbox_send(struct kernel_mailbox *mailbox, uint32_t message) {
  if (!kernel_mailbox_is_valid(mailbox)) {
    return;
  }

  while (!kernel_mailbox_try_send(mailbox, message)) {
    kernel_wait_queue_wait(mailbox->sender_wait_queue);
  }
}

int kernel_mailbox_try_receive(struct kernel_mailbox *mailbox, uint32_t *message) {
  if (!kernel_mailbox_is_valid(mailbox) || (mailbox->has_message == 0u)) {
    return 0;
  }

  if (message != 0) {
    *message = mailbox->message;
  }
  mailbox->has_message = 0u;
  if (kernel_wait_queue_waiter_count(mailbox->sender_wait_queue) != 0u) {
    (void)kernel_wait_queue_wake_one(mailbox->sender_wait_queue);
  }
  return 1;
}

uint32_t kernel_mailbox_receive(struct kernel_mailbox *mailbox) {
  uint32_t message = 0u;

  if (!kernel_mailbox_is_valid(mailbox)) {
    return 0u;
  }

  while (!kernel_mailbox_try_receive(mailbox, &message)) {
    kernel_wait_queue_wait(mailbox->receiver_wait_queue);
  }

  return message;
}

int kernel_mailbox_has_message(const struct kernel_mailbox *mailbox) {
  return kernel_mailbox_is_valid(mailbox) && (mailbox->has_message != 0u);
}

uint32_t kernel_mailbox_waiting_senders(const struct kernel_mailbox *mailbox) {
  return kernel_mailbox_is_valid(mailbox)
             ? kernel_wait_queue_waiter_count(mailbox->sender_wait_queue)
             : 0u;
}

uint32_t kernel_mailbox_waiting_receivers(const struct kernel_mailbox *mailbox) {
  return kernel_mailbox_is_valid(mailbox)
             ? kernel_wait_queue_waiter_count(mailbox->receiver_wait_queue)
             : 0u;
}
