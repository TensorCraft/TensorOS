#include "runtime.h"

struct scheduler_preempt_state {
  uint32_t pending;
  uint32_t tick_count;
  uint32_t disable_depth;
};

static struct scheduler_preempt_state g_scheduler_preempt_state;

__attribute__((weak)) int scheduler_target_preempt_capable(void) {
  return CONFIG_TARGET_PREEMPT_SAFE_POINT_CAPABLE != 0;
}

__attribute__((weak)) void scheduler_target_preempt_safe_point(void) {
}

void scheduler_preempt_init(void) {
  g_scheduler_preempt_state.pending = 0u;
  g_scheduler_preempt_state.tick_count = 0u;
  g_scheduler_preempt_state.disable_depth = 0u;
}

void scheduler_preempt_on_timer_tick(void) {
  ++g_scheduler_preempt_state.tick_count;

#if CONFIG_SCHEDULER_PREEMPTIVE
  g_scheduler_preempt_state.pending = 1u;
#endif
}

int scheduler_preempt_supported(void) {
#if CONFIG_SCHEDULER_PREEMPTIVE
  return 1;
#else
  return 0;
#endif
}

int scheduler_preempt_target_capable(void) {
  return scheduler_target_preempt_capable() != 0;
}

int scheduler_preempt_pending(void) {
  return g_scheduler_preempt_state.pending != 0u;
}

void scheduler_preempt_clear_pending(void) {
  g_scheduler_preempt_state.pending = 0u;
}

int scheduler_maybe_preempt_at_safe_point(void) {
  if (!scheduler_preempt_supported()) {
    return 0;
  }

  if (!scheduler_preempt_pending()) {
    return 0;
  }

  if (g_scheduler_preempt_state.disable_depth != 0u) {
    return 0;
  }

  if (!scheduler_preempt_target_capable()) {
    return 0;
  }

  g_scheduler_preempt_state.pending = 0u;
  scheduler_target_preempt_safe_point();
  return 1;
}

void preempt_disable(void) {
  ++g_scheduler_preempt_state.disable_depth;
}

void preempt_enable(void) {
  if (g_scheduler_preempt_state.disable_depth == 0u) {
    return;
  }

  --g_scheduler_preempt_state.disable_depth;
  if (g_scheduler_preempt_state.disable_depth == 0u) {
    (void)scheduler_maybe_preempt_at_safe_point();
  }
}

uint32_t preempt_disable_depth(void) {
  return g_scheduler_preempt_state.disable_depth;
}

uint32_t scheduler_preempt_tick_count(void) {
  return g_scheduler_preempt_state.tick_count;
}

const char *scheduler_preempt_status_name(void) {
  if (!scheduler_preempt_supported()) {
    return "cooperative_only";
  }

  if (!scheduler_preempt_target_capable()) {
    return "preempt_target_hook_missing";
  }

  if (g_scheduler_preempt_state.disable_depth != 0u) {
    return "preempt_deferred_critical_section";
  }

  if (scheduler_preempt_pending()) {
    return "preempt_pending_safe_point";
  }

  return "preempt_safe_point_idle";
}
