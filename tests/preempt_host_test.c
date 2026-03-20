#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEST_PREEMPT_TARGET_CAPABLE
#define TEST_PREEMPT_TARGET_CAPABLE 0
#endif

static uint32_t g_safe_point_hook_count __attribute__((unused));

#if TEST_PREEMPT_TARGET_CAPABLE
int scheduler_target_preempt_capable(void) {
  return 1;
}

void scheduler_target_preempt_safe_point(void) {
  ++g_safe_point_hook_count;
}
#endif

static void expect(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "preempt_host_test failed: %s\n", message);
    exit(1);
  }
}

int main(void) {
  scheduler_preempt_init();
  expect(scheduler_preempt_tick_count() == 0u, "initial tick count");
  expect(scheduler_preempt_pending() == 0, "initial pending state");
  expect(preempt_disable_depth() == 0u, "initial disable depth");
  preempt_enable();
  expect(preempt_disable_depth() == 0u, "preempt enable underflow is ignored");

  scheduler_preempt_on_timer_tick();
  expect(scheduler_preempt_tick_count() == 1u, "tick accounting");

#if CONFIG_SCHEDULER_PREEMPTIVE
  expect(scheduler_preempt_supported() == 1, "preemptive mode is reported");
  expect(scheduler_preempt_target_capable() == TEST_PREEMPT_TARGET_CAPABLE,
         "target capability follows build contract");
  expect(scheduler_preempt_pending() == 1, "preemptive mode latches pending state");
  if (TEST_PREEMPT_TARGET_CAPABLE != 0) {
    expect(strcmp(scheduler_preempt_status_name(), "preempt_pending_safe_point") == 0,
           "preemptive target-capable status name");
    expect(scheduler_maybe_preempt_at_safe_point() == 1,
           "target-capable safe point consumes pending preemption");
    expect(scheduler_preempt_pending() == 0, "safe point clears pending state");
    expect(g_safe_point_hook_count == 1u, "target hook called once");

    scheduler_preempt_on_timer_tick();
    preempt_disable();
    preempt_disable();
    expect(preempt_disable_depth() == 2u, "nested preempt disable depth");
    expect(strcmp(scheduler_preempt_status_name(), "preempt_deferred_critical_section") == 0,
           "critical section status name");
    expect(scheduler_maybe_preempt_at_safe_point() == 0,
           "critical section blocks safe-point consumption");
    expect(scheduler_preempt_pending() == 1, "pending remains latched while disabled");
    preempt_enable();
    expect(preempt_disable_depth() == 1u, "preempt enable unwinds one level");
    expect(g_safe_point_hook_count == 1u, "hook not called before outermost enable");
    preempt_enable();
    expect(preempt_disable_depth() == 0u, "preempt enable unwinds outermost level");
    expect(scheduler_preempt_pending() == 0, "outermost enable consumes deferred pending");
    expect(g_safe_point_hook_count == 2u, "hook called when outermost disable ends");
    expect(strcmp(scheduler_preempt_status_name(), "preempt_safe_point_idle") == 0,
           "idle safe-point status name");
  } else {
    expect(strcmp(scheduler_preempt_status_name(), "preempt_target_hook_missing") == 0,
           "preemptive target-incapable status name");
    expect(scheduler_maybe_preempt_at_safe_point() == 0,
           "target-incapable build does not consume pending preemption");
    expect(scheduler_preempt_pending() == 1, "pending remains until cleared");
    scheduler_preempt_clear_pending();
    expect(scheduler_preempt_pending() == 0, "pending state clears");
  }
#else
  expect(scheduler_preempt_supported() == 0, "cooperative mode is reported");
  expect(scheduler_preempt_target_capable() == 0, "cooperative target capability stays disabled");
  expect(scheduler_preempt_pending() == 0, "cooperative mode keeps pending clear");
  expect(strcmp(scheduler_preempt_status_name(), "cooperative_only") == 0,
         "cooperative status name");
  preempt_disable();
  expect(preempt_disable_depth() == 1u, "cooperative disable depth increments");
  preempt_enable();
  expect(preempt_disable_depth() == 0u, "cooperative disable depth unwinds");
#endif

  puts("preempt host checks passed");
  return 0;
}
