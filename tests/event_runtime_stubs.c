#include "runtime.h"

static uint32_t g_stub_wake_count = 3u;
static process_id_t g_stub_current_pid = 1u;
static uint32_t g_stub_wake_one_calls;
static void (*g_stub_wait_hook)(uint32_t channel);

void test_runtime_set_current_pid(process_id_t pid) {
  g_stub_current_pid = pid;
}

void test_runtime_set_wait_hook(void (*hook)(uint32_t channel)) {
  g_stub_wait_hook = hook;
}

void test_runtime_set_wake_count(uint32_t wake_count) {
  g_stub_wake_count = wake_count;
}

void test_runtime_reset_wake_one_calls(void) {
  g_stub_wake_one_calls = 0u;
}

uint32_t test_runtime_wake_one_calls(void) {
  return g_stub_wake_one_calls;
}

void process_wait_channel(uint32_t channel) {
  if (g_stub_wait_hook != 0) {
    g_stub_wait_hook(channel);
  }
}

uint32_t process_wake_channel(uint32_t channel) {
  (void)channel;
  return g_stub_wake_count;
}

uint32_t process_wake_first_channel(uint32_t channel) {
  (void)channel;
  ++g_stub_wake_one_calls;
  return 1u;
}

process_id_t process_current_pid(void) {
  return g_stub_current_pid;
}
