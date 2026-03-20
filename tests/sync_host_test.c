#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>

void test_runtime_set_current_pid(process_id_t pid);
void test_runtime_set_wait_hook(void (*hook)(uint32_t channel));
void test_runtime_set_wake_count(uint32_t wake_count);
void test_runtime_reset_wake_one_calls(void);
uint32_t test_runtime_wake_one_calls(void);

static void expect(int condition, const char *message);

static struct kernel_mutex *g_wait_test_mutex;
static struct kernel_mailbox *g_wait_test_mailbox;

static void release_mutex_from_wait_hook(uint32_t channel) {
  (void)channel;

  test_runtime_set_current_pid(1u);
  expect(kernel_mutex_unlock(g_wait_test_mutex) == 1, "wait hook releases held mutex");
  test_runtime_set_current_pid(2u);
}

static void receive_mailbox_from_wait_hook(uint32_t channel) {
  uint32_t message = 0u;

  (void)channel;
  test_runtime_set_current_pid(1u);
  expect(kernel_mailbox_try_receive(g_wait_test_mailbox, &message) == 1,
         "wait hook receives queued mailbox message");
  expect(message == 0xA5A5u, "wait hook receives expected mailbox message");
  test_runtime_set_current_pid(2u);
}

static void send_mailbox_from_wait_hook(uint32_t channel) {
  (void)channel;

  test_runtime_set_current_pid(2u);
  expect(kernel_mailbox_try_send(g_wait_test_mailbox, 0x55AAu) == 1,
         "wait hook sends mailbox message");
  test_runtime_set_current_pid(1u);
}

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "sync_host_test failed: %s\n", message);
    exit(1);
  }
}

static void run_service_round_trip(struct kernel_mailbox *request_mailbox,
                                   struct kernel_mailbox *reply_mailbox,
                                   struct kernel_event *request_event,
                                   struct kernel_semaphore *reply_semaphore,
                                   struct kernel_mutex *state_mutex,
                                   uint32_t request,
                                   uint32_t *total_processed) {
  uint32_t received_request;
  uint32_t reply;

  test_runtime_set_current_pid(11u);
  kernel_mailbox_send(request_mailbox, request);
  expect(kernel_event_signal(request_event) == 3u, "service event wakes waiters");

  test_runtime_set_current_pid(22u);
  kernel_mutex_lock(state_mutex);
  received_request = kernel_mailbox_receive(request_mailbox);
  expect(received_request == request, "service receives expected request");
  *total_processed += received_request;
  expect(kernel_mutex_unlock(state_mutex) == 1, "service unlock succeeds");
  kernel_mailbox_send(reply_mailbox, *total_processed);
  expect(kernel_semaphore_release(reply_semaphore) == 1, "service posts reply semaphore");

  test_runtime_set_current_pid(11u);
  kernel_semaphore_acquire(reply_semaphore);
  reply = kernel_mailbox_receive(reply_mailbox);
  expect(reply == *total_processed, "client receives expected reply");
}

int main(void) {
  kmem_init();
  test_runtime_set_current_pid(1u);
  test_runtime_set_wait_hook(0);
  test_runtime_set_wake_count(3u);
  test_runtime_reset_wake_one_calls();

  uint32_t initial_free = kmem_free_bytes();
  struct kernel_wait_queue *wait_queue = kernel_wait_queue_create();
  struct kernel_event *event = kernel_event_create();
  struct kernel_semaphore *semaphore = kernel_semaphore_create(2u, 2u);
  struct kernel_mutex *mutex = kernel_mutex_create();
  struct kernel_mailbox *mailbox = kernel_mailbox_create();
  struct kernel_event *service_event = kernel_event_create();
  struct kernel_semaphore *service_reply_semaphore = kernel_semaphore_create(0u, 1u);
  struct kernel_mutex *service_mutex = kernel_mutex_create();
  struct kernel_mailbox *service_request_mailbox = kernel_mailbox_create();
  struct kernel_mailbox *service_reply_mailbox = kernel_mailbox_create();
  uint32_t total_processed = 0u;

  expect(wait_queue != 0, "allocate wait queue");
  expect(event != 0, "allocate event");
  expect(semaphore != 0, "allocate semaphore");
  expect(mutex != 0, "allocate mutex");
  expect(mailbox != 0, "allocate mailbox");
  expect(service_event != 0, "allocate service event");
  expect(service_reply_semaphore != 0, "allocate service reply semaphore");
  expect(service_mutex != 0, "allocate service mutex");
  expect(service_request_mailbox != 0, "allocate service request mailbox");
  expect(service_reply_mailbox != 0, "allocate service reply mailbox");
  expect(kmem_free_bytes() < initial_free, "sync objects consume allocator space");

  expect(kernel_wait_queue_waiter_count(wait_queue) == 0u, "new wait queue has no waiters");
  expect(kernel_wait_queue_wake_one(wait_queue) == 1u, "wake one forwards single waiter wake");
  expect(kernel_wait_queue_wake_all(wait_queue) == 3u, "wake all forwards broadcast wake");

  expect(kernel_event_signal(event) == 3u, "event signal still wakes all waiters");
  expect(kernel_event_waiter_count(event) == 0u, "new event has no waiters");

  expect(kernel_semaphore_count(semaphore) == 2u, "initial semaphore count");
  expect(kernel_semaphore_try_acquire(semaphore) == 1, "first acquire succeeds");
  expect(kernel_semaphore_try_acquire(semaphore) == 1, "second acquire succeeds");
  expect(kernel_semaphore_try_acquire(semaphore) == 0, "empty semaphore blocks try-acquire");
  expect(kernel_semaphore_count(semaphore) == 0u, "count reaches zero");
  expect(kernel_semaphore_release(semaphore) == 1, "release restores one token");
  expect(kernel_semaphore_count(semaphore) == 1u, "count increments after release");
  expect(kernel_semaphore_release(semaphore) == 1, "second release restores max token");
  expect(kernel_semaphore_release(semaphore) == 0, "release refuses overflow");
  expect(kernel_semaphore_waiter_count(semaphore) == 0u, "new semaphore has no waiters");

  expect(kernel_mutex_owner_pid(mutex) == 0u, "new mutex starts unlocked");
  expect(kernel_mutex_waiter_count(mutex) == 0u, "new mutex has no waiters");
  expect(kernel_mutex_try_lock(mutex) == 1, "mutex try-lock succeeds when unlocked");
  expect(kernel_mutex_owner_pid(mutex) == 1u, "mutex records owner pid");
  expect(kernel_mutex_try_lock(mutex) == 0, "mutex try-lock fails when already owned");
  expect(kernel_mutex_destroy(mutex) == 0, "destroy refuses owned mutex");

  g_wait_test_mutex = mutex;
  test_runtime_set_current_pid(2u);
  test_runtime_set_wait_hook(release_mutex_from_wait_hook);
  test_runtime_reset_wake_one_calls();
  kernel_mutex_lock(mutex);
  expect(kernel_mutex_owner_pid(mutex) == 2u, "mutex lock acquires after wait hook release");
  expect(kernel_mutex_waiter_count(mutex) == 0u, "mutex waiter count returns to zero");
  expect(test_runtime_wake_one_calls() == 1u, "mutex unlock wakes one waiter");
  test_runtime_set_wait_hook(0);

  test_runtime_set_current_pid(1u);
  expect(kernel_mutex_unlock(mutex) == 0, "non-owner cannot unlock mutex");
  test_runtime_set_current_pid(2u);
  expect(kernel_mutex_unlock(mutex) == 1, "mutex unlock succeeds for owner");
  expect(kernel_mutex_owner_pid(mutex) == 0u, "mutex unlock clears owner");
  expect(kernel_mutex_unlock(mutex) == 0, "mutex unlock fails when already unlocked");

  test_runtime_set_current_pid(1u);
  expect(kernel_mutex_destroy(mutex) == 1, "destroy mutex");

  expect(kernel_mailbox_has_message(mailbox) == 0, "new mailbox starts empty");
  expect(kernel_mailbox_waiting_senders(mailbox) == 0u, "new mailbox has no waiting senders");
  expect(kernel_mailbox_waiting_receivers(mailbox) == 0u, "new mailbox has no waiting receivers");
  expect(kernel_mailbox_try_send(mailbox, 0xA5A5u) == 1, "mailbox try-send succeeds when empty");
  expect(kernel_mailbox_has_message(mailbox) == 1, "mailbox reports message present");
  expect(kernel_mailbox_try_send(mailbox, 0x5A5Au) == 0, "mailbox refuses second queued message");
  expect(kernel_mailbox_destroy(mailbox) == 0, "destroy refuses non-empty mailbox");

  g_wait_test_mailbox = mailbox;
  test_runtime_set_current_pid(2u);
  test_runtime_set_wait_hook(receive_mailbox_from_wait_hook);
  test_runtime_reset_wake_one_calls();
  kernel_mailbox_send(mailbox, 0x5A5Au);
  expect(kernel_mailbox_has_message(mailbox) == 1, "mailbox send completes after receiver frees slot");
  expect(test_runtime_wake_one_calls() == 1u, "mailbox receive wakes one waiting sender");
  test_runtime_set_wait_hook(0);

  test_runtime_set_current_pid(1u);
  expect(kernel_mailbox_try_receive(mailbox, 0) == 1, "mailbox can receive without output pointer");
  expect(kernel_mailbox_has_message(mailbox) == 0, "mailbox becomes empty after receive");

  test_runtime_set_wait_hook(send_mailbox_from_wait_hook);
  test_runtime_reset_wake_one_calls();
  expect(kernel_mailbox_receive(mailbox) == 0x55AAu, "mailbox receive blocks until sender provides message");
  expect(kernel_mailbox_has_message(mailbox) == 0, "mailbox empty after blocking receive");
  expect(test_runtime_wake_one_calls() == 1u, "mailbox send wakes one waiting receiver");
  test_runtime_set_wait_hook(0);

  run_service_round_trip(service_request_mailbox, service_reply_mailbox, service_event,
                         service_reply_semaphore, service_mutex, 0x20u, &total_processed);
  run_service_round_trip(service_request_mailbox, service_reply_mailbox, service_event,
                         service_reply_semaphore, service_mutex, 0x21u, &total_processed);
  expect(total_processed == 0x41u, "service workflow accumulates shared state");
  expect(kernel_semaphore_count(service_reply_semaphore) == 0u,
         "service reply semaphore returns to empty");
  expect(kernel_mailbox_has_message(service_request_mailbox) == 0,
         "service request mailbox drains after workflow");
  expect(kernel_mailbox_has_message(service_reply_mailbox) == 0,
         "service reply mailbox drains after workflow");
  expect(kernel_mutex_owner_pid(service_mutex) == 0u, "service mutex returns unlocked");

  expect(kernel_mailbox_destroy(mailbox) == 1, "destroy mailbox");
  expect(kernel_mailbox_destroy(service_request_mailbox) == 1, "destroy service request mailbox");
  expect(kernel_mailbox_destroy(service_reply_mailbox) == 1, "destroy service reply mailbox");
  expect(kernel_mutex_destroy(service_mutex) == 1, "destroy service mutex");
  expect(kernel_semaphore_destroy(service_reply_semaphore) == 1, "destroy service reply semaphore");
  expect(kernel_event_destroy(service_event) == 1, "destroy service event");
  expect(kernel_semaphore_destroy(semaphore) == 1, "destroy semaphore");
  expect(kernel_event_destroy(event) == 1, "destroy event");
  expect(kernel_wait_queue_destroy(wait_queue) == 1, "destroy wait queue");
  expect(kmem_free_bytes() >= initial_free - 64u, "allocator space mostly restored");

  puts("sync host checks passed");
  return 0;
}
