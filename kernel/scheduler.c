#include "runtime.h"
#include "process_core.h"
#include "runtime_policy.h"
#include "soc.h"

#include <limits.h>

#define MAX_PROCESSES 10u
#define PROCESS_STACK_WORDS 1024u
#define PROCESS_CONTEXT_WORDS (sizeof(struct task_context) / sizeof(uint32_t))
#define SCHED_RB_RED 1u
#define SCHED_RB_BLACK 0u

struct task_context {
  uint32_t ra;
  uint32_t s0;
  uint32_t s1;
  uint32_t s2;
  uint32_t s3;
  uint32_t s4;
  uint32_t s5;
  uint32_t s6;
  uint32_t s7;
  uint32_t s8;
  uint32_t s9;
  uint32_t s10;
  uint32_t s11;
};

struct process_execution_slot {
  task_entry_t entry;
  uint32_t *stack_pointer;
};

extern void task_bootstrap_asm(void);
extern void task_bootstrap_entry_asm(void);
extern void task_bootstrap_first_entry_asm(void);

static struct process_runtime_state g_processes[MAX_PROCESSES];
static struct process_execution_slot g_process_exec[MAX_PROCESSES];
static uint32_t g_process_stacks[MAX_PROCESSES][PROCESS_STACK_WORDS]
    __attribute__((aligned(16), section(".task_stacks")));
static uint32_t g_current_process_index = UINT32_MAX;
static process_id_t g_next_pid = 1u;
static uint32_t g_switch_count;
static uint8_t g_systimer_pending_polled;
static uint16_t g_bootstrap_seen_mask;
static uint32_t g_last_switch_log_count;
static uint64_t g_sched_vruntime[MAX_PROCESSES];
static uint64_t g_sched_exec_start_tick[MAX_PROCESSES];
static uint32_t g_sched_rb_parent[MAX_PROCESSES];
static uint32_t g_sched_rb_left[MAX_PROCESSES];
static uint32_t g_sched_rb_right[MAX_PROCESSES];
static uint8_t g_sched_rb_color[MAX_PROCESSES];
static uint8_t g_sched_on_rq[MAX_PROCESSES];
static uint32_t g_sched_rq_root = UINT32_MAX;
static uint32_t g_sched_rq_leftmost = UINT32_MAX;

#define DRAM_LOW  0x3FC80000u
#define DRAM_HIGH 0x3FCE0000u
#define CPU_SYSTIMER_INTR_NUM 7u

static char g_sched_dbg_tag[] = "SCHED";
#if !CONFIG_SCHEDULER_PREEMPTIVE
static char g_sched_dbg_bootstrap[] = "task_bootstrap_dram";
static char g_sched_dbg_bootstrap_pid[] = "bootstrap_pid=";
#endif
static char g_sched_dbg_switch_count[] = "switch_count=";
static char g_sched_dbg_switch_from[] = "switch_from_pid=";
static char g_sched_dbg_switch_to[] = "switch_to_pid=";
static char g_sched_dbg_run[] = "scheduler_run_dram";
static char g_sched_dbg_first_pick[] = "first_pick_pid=";
static char g_sched_dbg_first_start[] = "start_first_pid=";
static char g_sched_dbg_hw_switch[] = "hw_switch";
static uint8_t g_sched_verbose_logs;

static inline uint32_t interrupts_save_disable(void) {
  uint32_t mstatus;
  __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));
  return mstatus;
}

static inline void interrupts_restore(uint32_t mstatus) {
  if ((mstatus & BIT(3)) != 0u) {
    __asm__ volatile("csrsi mstatus, 8");
  }
}

static inline uint32_t scheduler_read_mstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

static inline void scheduler_wait_for_interrupt_window(uint32_t mstatus) {
  (void)mstatus;
}

static int scheduler_stack_pointer_in_range(uint32_t *sp) {
  uint32_t value = (uint32_t)(uintptr_t)sp;
  return (value >= DRAM_LOW) && (value < DRAM_HIGH);
}

static int scheduler_stack_pointer_is_aligned(uint32_t *sp) {
  return (((uint32_t)(uintptr_t)sp) & 0x3u) == 0u;
}

static struct process_runtime_state *scheduler_current_process(void) {
  if (g_current_process_index >= MAX_PROCESSES) {
    return 0;
  }

  if (g_processes[g_current_process_index].state == PROCESS_STATE_UNUSED) {
    return 0;
  }

  return &g_processes[g_current_process_index];
}

static struct process_runtime_state *scheduler_runtime_states(void) {
  return g_processes;
}

static const struct process_runtime_state *scheduler_runtime_states_const(void) {
  return g_processes;
}

static void scheduler_ready_queue_recompute_leftmost(void) {
  uint32_t index = g_sched_rq_root;

  if (index == UINT32_MAX) {
    g_sched_rq_leftmost = UINT32_MAX;
    return;
  }

  while (g_sched_rb_left[index] != UINT32_MAX) {
    index = g_sched_rb_left[index];
  }

  g_sched_rq_leftmost = index;
}

static int scheduler_vruntime_before(uint32_t lhs_index, uint32_t rhs_index) {
  if (g_sched_vruntime[lhs_index] < g_sched_vruntime[rhs_index]) {
    return 1;
  }
  if (g_sched_vruntime[lhs_index] > g_sched_vruntime[rhs_index]) {
    return 0;
  }
  if (g_processes[lhs_index].pid < g_processes[rhs_index].pid) {
    return 1;
  }
  if (g_processes[lhs_index].pid > g_processes[rhs_index].pid) {
    return 0;
  }
  return lhs_index < rhs_index;
}

static void scheduler_ready_queue_rotate_left(uint32_t index) {
  uint32_t right = g_sched_rb_right[index];
  uint32_t parent = g_sched_rb_parent[index];

  g_sched_rb_right[index] = g_sched_rb_left[right];
  if (g_sched_rb_left[right] != UINT32_MAX) {
    g_sched_rb_parent[g_sched_rb_left[right]] = index;
  }

  g_sched_rb_parent[right] = parent;
  if (parent == UINT32_MAX) {
    g_sched_rq_root = right;
  } else if (g_sched_rb_left[parent] == index) {
    g_sched_rb_left[parent] = right;
  } else {
    g_sched_rb_right[parent] = right;
  }

  g_sched_rb_left[right] = index;
  g_sched_rb_parent[index] = right;
}

static void scheduler_ready_queue_rotate_right(uint32_t index) {
  uint32_t left = g_sched_rb_left[index];
  uint32_t parent = g_sched_rb_parent[index];

  g_sched_rb_left[index] = g_sched_rb_right[left];
  if (g_sched_rb_right[left] != UINT32_MAX) {
    g_sched_rb_parent[g_sched_rb_right[left]] = index;
  }

  g_sched_rb_parent[left] = parent;
  if (parent == UINT32_MAX) {
    g_sched_rq_root = left;
  } else if (g_sched_rb_right[parent] == index) {
    g_sched_rb_right[parent] = left;
  } else {
    g_sched_rb_left[parent] = left;
  }

  g_sched_rb_right[left] = index;
  g_sched_rb_parent[index] = left;
}

static void scheduler_ready_queue_insert(uint32_t index) {
  uint32_t parent = UINT32_MAX;
  uint32_t cursor = g_sched_rq_root;

  if (g_sched_on_rq[index] != 0u) {
    return;
  }

  g_sched_rb_left[index] = UINT32_MAX;
  g_sched_rb_right[index] = UINT32_MAX;
  while (cursor != UINT32_MAX) {
    parent = cursor;
    if (scheduler_vruntime_before(index, cursor)) {
      cursor = g_sched_rb_left[cursor];
    } else {
      cursor = g_sched_rb_right[cursor];
    }
  }

  g_sched_rb_parent[index] = parent;
  g_sched_rb_color[index] = SCHED_RB_RED;
  g_sched_on_rq[index] = 1u;

  if (parent == UINT32_MAX) {
    g_sched_rq_root = index;
  } else if (scheduler_vruntime_before(index, parent)) {
    g_sched_rb_left[parent] = index;
  } else {
    g_sched_rb_right[parent] = index;
  }

  while ((index != g_sched_rq_root) &&
         (g_sched_rb_color[g_sched_rb_parent[index]] == SCHED_RB_RED)) {
    uint32_t parent_index = g_sched_rb_parent[index];
    uint32_t grandparent = g_sched_rb_parent[parent_index];

    if (g_sched_rb_left[grandparent] == parent_index) {
      uint32_t uncle = g_sched_rb_right[grandparent];
      if ((uncle != UINT32_MAX) && (g_sched_rb_color[uncle] == SCHED_RB_RED)) {
        g_sched_rb_color[parent_index] = SCHED_RB_BLACK;
        g_sched_rb_color[uncle] = SCHED_RB_BLACK;
        g_sched_rb_color[grandparent] = SCHED_RB_RED;
        index = grandparent;
      } else {
        if (g_sched_rb_right[parent_index] == index) {
          index = parent_index;
          scheduler_ready_queue_rotate_left(index);
          parent_index = g_sched_rb_parent[index];
          grandparent = g_sched_rb_parent[parent_index];
        }
        g_sched_rb_color[parent_index] = SCHED_RB_BLACK;
        g_sched_rb_color[grandparent] = SCHED_RB_RED;
        scheduler_ready_queue_rotate_right(grandparent);
      }
    } else {
      uint32_t uncle = g_sched_rb_left[grandparent];
      if ((uncle != UINT32_MAX) && (g_sched_rb_color[uncle] == SCHED_RB_RED)) {
        g_sched_rb_color[parent_index] = SCHED_RB_BLACK;
        g_sched_rb_color[uncle] = SCHED_RB_BLACK;
        g_sched_rb_color[grandparent] = SCHED_RB_RED;
        index = grandparent;
      } else {
        if (g_sched_rb_left[parent_index] == index) {
          index = parent_index;
          scheduler_ready_queue_rotate_right(index);
          parent_index = g_sched_rb_parent[index];
          grandparent = g_sched_rb_parent[parent_index];
        }
        g_sched_rb_color[parent_index] = SCHED_RB_BLACK;
        g_sched_rb_color[grandparent] = SCHED_RB_RED;
        scheduler_ready_queue_rotate_left(grandparent);
      }
    }
  }

  g_sched_rb_color[g_sched_rq_root] = SCHED_RB_BLACK;
  scheduler_ready_queue_recompute_leftmost();
}

static uint32_t scheduler_ready_queue_minimum(uint32_t index) {
  while ((index != UINT32_MAX) && (g_sched_rb_left[index] != UINT32_MAX)) {
    index = g_sched_rb_left[index];
  }
  return index;
}

static void scheduler_ready_queue_transplant(uint32_t old_index, uint32_t new_index) {
  uint32_t parent = g_sched_rb_parent[old_index];

  if (parent == UINT32_MAX) {
    g_sched_rq_root = new_index;
  } else if (g_sched_rb_left[parent] == old_index) {
    g_sched_rb_left[parent] = new_index;
  } else {
    g_sched_rb_right[parent] = new_index;
  }

  if (new_index != UINT32_MAX) {
    g_sched_rb_parent[new_index] = parent;
  }
}

static void scheduler_ready_queue_remove_fixup(uint32_t index, uint32_t parent) {
  while ((index != g_sched_rq_root) &&
         ((index == UINT32_MAX) || (g_sched_rb_color[index] == SCHED_RB_BLACK))) {
    if (parent == UINT32_MAX) {
      break;
    }

    if (index == g_sched_rb_left[parent]) {
      uint32_t sibling = g_sched_rb_right[parent];

      if ((sibling != UINT32_MAX) && (g_sched_rb_color[sibling] == SCHED_RB_RED)) {
        g_sched_rb_color[sibling] = SCHED_RB_BLACK;
        g_sched_rb_color[parent] = SCHED_RB_RED;
        scheduler_ready_queue_rotate_left(parent);
        sibling = g_sched_rb_right[parent];
      }

      if ((sibling == UINT32_MAX) ||
          (((g_sched_rb_left[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_left[sibling]] == SCHED_RB_BLACK)) &&
           ((g_sched_rb_right[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_right[sibling]] == SCHED_RB_BLACK)))) {
        if (sibling != UINT32_MAX) {
          g_sched_rb_color[sibling] = SCHED_RB_RED;
        }
        index = parent;
        parent = g_sched_rb_parent[index];
      } else {
        if ((g_sched_rb_right[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_right[sibling]] == SCHED_RB_BLACK)) {
          if (g_sched_rb_left[sibling] != UINT32_MAX) {
            g_sched_rb_color[g_sched_rb_left[sibling]] = SCHED_RB_BLACK;
          }
          g_sched_rb_color[sibling] = SCHED_RB_RED;
          scheduler_ready_queue_rotate_right(sibling);
          sibling = g_sched_rb_right[parent];
        }

        g_sched_rb_color[sibling] = g_sched_rb_color[parent];
        g_sched_rb_color[parent] = SCHED_RB_BLACK;
        if (g_sched_rb_right[sibling] != UINT32_MAX) {
          g_sched_rb_color[g_sched_rb_right[sibling]] = SCHED_RB_BLACK;
        }
        scheduler_ready_queue_rotate_left(parent);
        index = g_sched_rq_root;
        break;
      }
    } else {
      uint32_t sibling = g_sched_rb_left[parent];

      if ((sibling != UINT32_MAX) && (g_sched_rb_color[sibling] == SCHED_RB_RED)) {
        g_sched_rb_color[sibling] = SCHED_RB_BLACK;
        g_sched_rb_color[parent] = SCHED_RB_RED;
        scheduler_ready_queue_rotate_right(parent);
        sibling = g_sched_rb_left[parent];
      }

      if ((sibling == UINT32_MAX) ||
          (((g_sched_rb_left[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_left[sibling]] == SCHED_RB_BLACK)) &&
           ((g_sched_rb_right[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_right[sibling]] == SCHED_RB_BLACK)))) {
        if (sibling != UINT32_MAX) {
          g_sched_rb_color[sibling] = SCHED_RB_RED;
        }
        index = parent;
        parent = g_sched_rb_parent[index];
      } else {
        if ((g_sched_rb_left[sibling] == UINT32_MAX) ||
            (g_sched_rb_color[g_sched_rb_left[sibling]] == SCHED_RB_BLACK)) {
          if (g_sched_rb_right[sibling] != UINT32_MAX) {
            g_sched_rb_color[g_sched_rb_right[sibling]] = SCHED_RB_BLACK;
          }
          g_sched_rb_color[sibling] = SCHED_RB_RED;
          scheduler_ready_queue_rotate_left(sibling);
          sibling = g_sched_rb_left[parent];
        }

        g_sched_rb_color[sibling] = g_sched_rb_color[parent];
        g_sched_rb_color[parent] = SCHED_RB_BLACK;
        if (g_sched_rb_left[sibling] != UINT32_MAX) {
          g_sched_rb_color[g_sched_rb_left[sibling]] = SCHED_RB_BLACK;
        }
        scheduler_ready_queue_rotate_right(parent);
        index = g_sched_rq_root;
        break;
      }
    }
  }

  if (index != UINT32_MAX) {
    g_sched_rb_color[index] = SCHED_RB_BLACK;
  }
}

static void scheduler_ready_queue_remove(uint32_t index) {
  uint32_t replacement;
  uint32_t replacement_parent;
  uint32_t removed_index = index;
  uint8_t removed_color;

  if (g_sched_on_rq[index] == 0u) {
    return;
  }

  removed_color = g_sched_rb_color[removed_index];
  if (g_sched_rb_left[index] == UINT32_MAX) {
    replacement = g_sched_rb_right[index];
    replacement_parent = g_sched_rb_parent[index];
    scheduler_ready_queue_transplant(index, g_sched_rb_right[index]);
  } else if (g_sched_rb_right[index] == UINT32_MAX) {
    replacement = g_sched_rb_left[index];
    replacement_parent = g_sched_rb_parent[index];
    scheduler_ready_queue_transplant(index, g_sched_rb_left[index]);
  } else {
    removed_index = scheduler_ready_queue_minimum(g_sched_rb_right[index]);
    removed_color = g_sched_rb_color[removed_index];
    replacement = g_sched_rb_right[removed_index];

    if (g_sched_rb_parent[removed_index] == index) {
      replacement_parent = removed_index;
      if (replacement != UINT32_MAX) {
        g_sched_rb_parent[replacement] = removed_index;
      }
    } else {
      replacement_parent = g_sched_rb_parent[removed_index];
      scheduler_ready_queue_transplant(removed_index, g_sched_rb_right[removed_index]);
      g_sched_rb_right[removed_index] = g_sched_rb_right[index];
      g_sched_rb_parent[g_sched_rb_right[removed_index]] = removed_index;
    }

    scheduler_ready_queue_transplant(index, removed_index);
    g_sched_rb_left[removed_index] = g_sched_rb_left[index];
    g_sched_rb_parent[g_sched_rb_left[removed_index]] = removed_index;
    g_sched_rb_color[removed_index] = g_sched_rb_color[index];
  }

  g_sched_on_rq[index] = 0u;
  g_sched_rb_parent[index] = UINT32_MAX;
  g_sched_rb_left[index] = UINT32_MAX;
  g_sched_rb_right[index] = UINT32_MAX;
  g_sched_rb_color[index] = SCHED_RB_BLACK;

  if (removed_color == SCHED_RB_BLACK) {
    scheduler_ready_queue_remove_fixup(replacement, replacement_parent);
  }

  scheduler_ready_queue_recompute_leftmost();
}

static uint64_t scheduler_min_vruntime(void) {
  uint64_t min_vruntime = 0u;
  uint8_t have_value = 0u;

  if (g_sched_rq_leftmost != UINT32_MAX) {
    min_vruntime = g_sched_vruntime[g_sched_rq_leftmost];
    have_value = 1u;
  }

  if ((g_current_process_index < MAX_PROCESSES) &&
      (g_processes[g_current_process_index].state == PROCESS_STATE_RUNNING)) {
    if ((have_value == 0u) || (g_sched_vruntime[g_current_process_index] < min_vruntime)) {
      min_vruntime = g_sched_vruntime[g_current_process_index];
      have_value = 1u;
    }
  }

  return have_value != 0u ? min_vruntime : 0u;
}

static void scheduler_account_runtime(uint32_t index) {
  uint64_t now;
  uint64_t started;

  if (index >= MAX_PROCESSES) {
    return;
  }

  if (g_processes[index].state != PROCESS_STATE_RUNNING) {
    return;
  }

  now = kernel_ticks_read();
  started = g_sched_exec_start_tick[index];
  if (now > started) {
    g_sched_vruntime[index] += now - started;
  } else {
    g_sched_vruntime[index] += 1u;
  }
  g_sched_exec_start_tick[index] = now;
}

static void scheduler_sync_ready_queue(void) {
  for (uint32_t index = 0u; index < MAX_PROCESSES; ++index) {
    if (g_processes[index].state == PROCESS_STATE_READY) {
      scheduler_ready_queue_insert(index);
    } else {
      scheduler_ready_queue_remove(index);
    }
  }
}

static uint32_t scheduler_pick_next_ready(void) {
  return g_sched_rq_leftmost;
}

static void scheduler_reset_process_slot(uint32_t index) {
  scheduler_ready_queue_remove(index);
  process_core_reset_slot(&g_processes[index]);
  g_process_exec[index].entry = 0;
  g_process_exec[index].stack_pointer = 0;
  g_sched_vruntime[index] = 0u;
  g_sched_exec_start_tick[index] = 0u;
  g_sched_rb_parent[index] = UINT32_MAX;
  g_sched_rb_left[index] = UINT32_MAX;
  g_sched_rb_right[index] = UINT32_MAX;
  g_sched_rb_color[index] = SCHED_RB_BLACK;
  g_sched_on_rq[index] = 0u;
}

static void scheduler_make_process_ready(uint32_t index) {
  process_core_make_ready(&g_processes[index]);
  scheduler_sync_ready_queue();
}

static void scheduler_block_process(uint32_t index, uint32_t wait_reason, uint32_t wait_channel,
                                    uint64_t wake_tick, process_id_t wait_target_pid) {
  process_core_block(&g_processes[index], wait_reason, wait_channel, wake_tick, wait_target_pid);
  scheduler_sync_ready_queue();
}

static void scheduler_prepare_initial_context(uint32_t index) {
  struct task_context *context =
      (struct task_context *)(&g_process_stacks[index][PROCESS_STACK_WORDS] - PROCESS_CONTEXT_WORDS);

  context->ra = (uint32_t)(uintptr_t)task_bootstrap_first_entry_asm;
  context->s0 = 0u;
  context->s1 = 0u;
  context->s2 = 0u;
  context->s3 = 0u;
  context->s4 = 0u;
  context->s5 = 0u;
  context->s6 = 0u;
  context->s7 = 0u;
  context->s8 = 0u;
  context->s9 = 0u;
  context->s10 = 0u;
  context->s11 = 0u;

  g_process_exec[index].stack_pointer = (uint32_t *)context;
}

static void scheduler_start_first_process(uint32_t process_index) {
  uint32_t mstatus = interrupts_save_disable();

  if (process_index >= MAX_PROCESSES) {
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_NEXT_INDEX);
    reg_write(RTC_CNTL_STORE7_REG, process_index);
    panic_halt();
  }

  if (!scheduler_stack_pointer_in_range(g_process_exec[process_index].stack_pointer)) {
    reg_write(RTC_CNTL_STORE0_REG, 0xF602u);
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_TASK_SP_RANGE);
    reg_write(RTC_CNTL_STORE7_REG, (uint32_t)(uintptr_t)g_process_exec[process_index].stack_pointer);
    panic_halt();
  }

  if (!scheduler_stack_pointer_is_aligned(g_process_exec[process_index].stack_pointer)) {
    reg_write(RTC_CNTL_STORE0_REG, 0xF603u);
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_TASK_SP_ALIGN);
    reg_write(RTC_CNTL_STORE7_REG, (uint32_t)(uintptr_t)g_process_exec[process_index].stack_pointer);
    panic_halt();
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF604u);
  reg_write(RTC_CNTL_STORE1_REG, g_processes[process_index].pid);
  reg_write(RTC_CNTL_STORE2_REG, (uint32_t)(uintptr_t)g_process_exec[process_index].stack_pointer);
  reg_write(RTC_CNTL_STORE3_REG, g_process_exec[process_index].stack_pointer[0]);
  if (g_sched_verbose_logs != 0u) {
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_first_start, g_processes[process_index].pid);
  }
  g_current_process_index = process_index;
  g_processes[process_index].state = PROCESS_STATE_RUNNING;
  g_sched_exec_start_tick[process_index] = kernel_ticks_read();
  scheduler_sync_ready_queue();
  task_start_first(g_process_exec[process_index].stack_pointer);
  interrupts_restore(mstatus);
  panic_halt();
}

static void scheduler_switch_process(uint32_t current_index, uint32_t next_index) {
  struct process_runtime_state *current = &g_processes[current_index];
  struct process_runtime_state *next = &g_processes[next_index];

  scheduler_account_runtime(current_index);
  if (current->state == PROCESS_STATE_RUNNING) {
    current->state = PROCESS_STATE_READY;
  }

  next->state = PROCESS_STATE_RUNNING;
  ++next->switch_count;
  g_current_process_index = next_index;
  g_sched_exec_start_tick[next_index] = kernel_ticks_read();
  scheduler_sync_ready_queue();
  context_switch(&g_process_exec[current_index].stack_pointer, g_process_exec[next_index].stack_pointer);
}

static void scheduler_validate_switch_targets(uint32_t current_index, uint32_t next_index) {
  if (current_index >= MAX_PROCESSES) {
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_CURRENT_INDEX);
    reg_write(RTC_CNTL_STORE7_REG, current_index);
    panic_halt();
  }

  if (next_index >= MAX_PROCESSES) {
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_NEXT_INDEX);
    reg_write(RTC_CNTL_STORE7_REG, next_index);
    panic_halt();
  }

  if (!scheduler_stack_pointer_in_range(g_process_exec[current_index].stack_pointer) ||
      !scheduler_stack_pointer_in_range(g_process_exec[next_index].stack_pointer)) {
    reg_write(RTC_CNTL_STORE0_REG, 0xE301u);
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_TASK_SP_RANGE);
    reg_write(RTC_CNTL_STORE7_REG, (uint32_t)(uintptr_t)g_process_exec[next_index].stack_pointer);
    panic_halt();
  }

  if (!scheduler_stack_pointer_is_aligned(g_process_exec[current_index].stack_pointer) ||
      !scheduler_stack_pointer_is_aligned(g_process_exec[next_index].stack_pointer)) {
    reg_write(RTC_CNTL_STORE0_REG, 0xE301u);
    reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_BAD_TASK_SP_ALIGN);
    reg_write(RTC_CNTL_STORE7_REG, (uint32_t)(uintptr_t)g_process_exec[next_index].stack_pointer);
    panic_halt();
  }
}

process_id_t process_spawn_with_role(const char *name, task_entry_t entry, uint32_t role) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t index = process_core_find_free_slot(scheduler_runtime_states_const(), MAX_PROCESSES);
  process_id_t parent_pid = process_current_pid();

  if ((entry == 0) || (index == UINT32_MAX) || (g_next_pid == 0u)) {
    interrupts_restore(mstatus);
    return 0u;
  }

  if (!runtime_policy_can_spawn(scheduler_runtime_states_const(), MAX_PROCESSES, role)) {
    interrupts_restore(mstatus);
    return 0u;
  }

  g_processes[index].pid = g_next_pid++;
  g_processes[index].parent_pid = parent_pid;
  g_processes[index].name = (name != 0) ? name : "PROC";
  g_processes[index].role = role;
  g_process_exec[index].entry = entry;
  g_processes[index].state = PROCESS_STATE_READY;
  g_processes[index].exit_code = 0;
  g_processes[index].switch_count = 0u;
  g_processes[index].yield_count = 0u;
  g_processes[index].wake_tick = 0u;
  g_processes[index].wait_target_pid = 0u;
  g_processes[index].wait_reason = PROCESS_WAIT_NONE;
  g_processes[index].wait_channel = 0u;
  g_sched_vruntime[index] = scheduler_min_vruntime();
  g_sched_exec_start_tick[index] = 0u;
  scheduler_prepare_initial_context(index);
  scheduler_sync_ready_queue();

  interrupts_restore(mstatus);
  return g_processes[index].pid;
}

process_id_t process_spawn(const char *name, task_entry_t entry) {
  return process_spawn_with_role(name, entry, RUNTIME_PROCESS_ROLE_SYSTEM);
}

void process_exit(int32_t exit_code) {
  uint32_t mstatus = interrupts_save_disable();
  struct process_runtime_state *current = scheduler_current_process();

  if (current != 0) {
    current->state = PROCESS_STATE_ZOMBIE;
    current->exit_code = exit_code;
    process_core_wake_waiting_parent(scheduler_runtime_states(), MAX_PROCESSES, current->pid);
    console_log(current->name, "exited");
  }

  interrupts_restore(mstatus);
  process_yield();
  panic_halt();
}

int process_kill(process_id_t pid, int32_t exit_code) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t index = process_core_find_index_by_pid(scheduler_runtime_states_const(), MAX_PROCESSES, pid);

  if (index == UINT32_MAX) {
    interrupts_restore(mstatus);
    return 0;
  }

  if (index == g_current_process_index) {
    interrupts_restore(mstatus);
    process_exit(exit_code);
    return 1;
  }

  if ((g_processes[index].state == PROCESS_STATE_READY) ||
      (g_processes[index].state == PROCESS_STATE_RUNNING) ||
      (g_processes[index].state == PROCESS_STATE_BLOCKED)) {
    g_processes[index].state = PROCESS_STATE_ZOMBIE;
    g_processes[index].exit_code = exit_code;
    process_core_wake_waiting_parent(scheduler_runtime_states(), MAX_PROCESSES, pid);
    scheduler_sync_ready_queue();
  }

  interrupts_restore(mstatus);
  return 1;
}

int process_reap(process_id_t pid, int32_t *exit_code) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t index = process_core_find_index_by_pid(scheduler_runtime_states_const(), MAX_PROCESSES, pid);

  if ((index == UINT32_MAX) || (g_processes[index].state != PROCESS_STATE_ZOMBIE)) {
    interrupts_restore(mstatus);
    return 0;
  }

  if (exit_code != 0) {
    *exit_code = g_processes[index].exit_code;
  }

  scheduler_reset_process_slot(index);

  interrupts_restore(mstatus);
  return 1;
}

int process_waitpid(process_id_t pid, int32_t *exit_code) {
  process_id_t current_pid = process_current_pid();

  if (current_pid == 0u) {
    return -1;
  }

  for (;;) {
    uint32_t mstatus = interrupts_save_disable();
    uint32_t child_index = process_core_find_waitable_child(
        scheduler_runtime_states_const(), MAX_PROCESSES, current_pid, pid);

    if (child_index != UINT32_MAX) {
      process_id_t child_pid = g_processes[child_index].pid;
      if (exit_code != 0) {
        *exit_code = g_processes[child_index].exit_code;
      }
      scheduler_reset_process_slot(child_index);
      interrupts_restore(mstatus);
      return (int)child_pid;
    }

    if (!process_core_has_child(scheduler_runtime_states_const(), MAX_PROCESSES, current_pid, pid)) {
      interrupts_restore(mstatus);
      return -1;
    }

    if (g_current_process_index < MAX_PROCESSES) {
      scheduler_block_process(g_current_process_index, PROCESS_WAIT_CHILD, pid, 0u, pid);
    }
    interrupts_restore(mstatus);
    process_yield();
  }
}

int process_wait(int32_t *exit_code) {
  return process_waitpid(0u, exit_code);
}

void process_sleep(uint32_t ticks) {
  uint32_t mstatus = interrupts_save_disable();
  struct process_runtime_state *current = scheduler_current_process();

  if (current == 0) {
    interrupts_restore(mstatus);
    return;
  }

  if (ticks == 0u) {
    interrupts_restore(mstatus);
    process_yield();
    return;
  }

  current->wake_tick = kernel_ticks_read() + (uint64_t)ticks;
  scheduler_block_process(g_current_process_index, PROCESS_WAIT_SLEEP_TICKS, ticks,
                          current->wake_tick, 0u);
  interrupts_restore(mstatus);
  process_yield();
}

int process_block(process_id_t pid) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t index = process_core_find_index_by_pid(scheduler_runtime_states_const(), MAX_PROCESSES, pid);

  if (index == UINT32_MAX) {
    interrupts_restore(mstatus);
    return 0;
  }

  if ((g_processes[index].state != PROCESS_STATE_READY) &&
      (g_processes[index].state != PROCESS_STATE_RUNNING)) {
    interrupts_restore(mstatus);
    return 0;
  }

  if (index == g_current_process_index) {
    scheduler_block_process(index, PROCESS_WAIT_MANUAL, pid, 0u, 0u);
    interrupts_restore(mstatus);
    process_yield();
    return 1;
  }

  scheduler_block_process(index, PROCESS_WAIT_MANUAL, pid, 0u, 0u);
  interrupts_restore(mstatus);
  return 1;
}

int process_wake(process_id_t pid) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t index = process_core_find_index_by_pid(scheduler_runtime_states_const(), MAX_PROCESSES, pid);

  if (index == UINT32_MAX) {
    interrupts_restore(mstatus);
    return 0;
  }

  scheduler_make_process_ready(index);
  interrupts_restore(mstatus);
  return 1;
}

void process_wait_channel(uint32_t channel) {
  uint32_t mstatus = interrupts_save_disable();

  if (g_current_process_index >= MAX_PROCESSES) {
    interrupts_restore(mstatus);
    return;
  }

  scheduler_block_process(g_current_process_index, PROCESS_WAIT_EVENT_CHANNEL, channel, 0u, 0u);
  interrupts_restore(mstatus);
  process_yield();
}

uint32_t process_wake_channel(uint32_t channel) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t woke =
      process_core_wake_channel(scheduler_runtime_states(), MAX_PROCESSES, channel);
  scheduler_sync_ready_queue();
  interrupts_restore(mstatus);
  return woke;
}

uint32_t process_wake_first_channel(uint32_t channel) {
  uint32_t mstatus = interrupts_save_disable();
  uint32_t woke = process_core_wake_first_channel(scheduler_runtime_states(), MAX_PROCESSES,
                                                  channel, g_current_process_index);
  scheduler_sync_ready_queue();
  interrupts_restore(mstatus);
  return woke;
}

process_id_t process_current_pid(void) {
  struct process_runtime_state *current = scheduler_current_process();
  return (current != 0) ? current->pid : 0u;
}

uint32_t process_count(void) {
  uint32_t count = 0u;

  for (uint32_t index = 0u; index < MAX_PROCESSES; ++index) {
    if (g_processes[index].state != PROCESS_STATE_UNUSED) {
      ++count;
    }
  }

  return count;
}

uint32_t process_snapshot(struct process_info *buffer, uint32_t capacity) {
  return process_core_snapshot(scheduler_runtime_states_const(), MAX_PROCESSES, buffer, capacity);
}

void process_tick(uint64_t tick_count) {
  uint32_t mstatus = interrupts_save_disable();

  scheduler_preempt_on_timer_tick();
  process_core_wake_sleepers(scheduler_runtime_states(), MAX_PROCESSES, tick_count);
  scheduler_sync_ready_queue();

  interrupts_restore(mstatus);
}

void process_yield(void) {
  struct process_runtime_state *current = scheduler_current_process();
  uint32_t current_index = g_current_process_index;
  uint32_t next_index;
  process_id_t current_pid = (current != 0) ? current->pid : 0u;

  if (current_index == UINT32_MAX) {
    runtime_poll_timer_delivery();
    reg_write(RTC_CNTL_STORE0_REG, 0xF600u);
    next_index = scheduler_pick_next_ready();
    reg_write(RTC_CNTL_STORE0_REG, 0xF601u);
    reg_write(RTC_CNTL_STORE1_REG, next_index);
    reg_write(RTC_CNTL_STORE2_REG,
              (next_index < MAX_PROCESSES) ? g_processes[next_index].pid : 0u);
    if (g_sched_verbose_logs != 0u) {
      console_log_u32(g_sched_dbg_tag, g_sched_dbg_first_pick, g_processes[next_index].pid);
    }
    scheduler_start_first_process(next_index);
  }

  preempt_disable();
  runtime_poll_timer_delivery();

  if (current != 0) {
    ++current->yield_count;
  }

  (void)scheduler_maybe_preempt_at_safe_point();

  next_index = scheduler_pick_next_ready();

  if (current_pid == 1u) {
    reg_write(RTC_CNTL_STORE6_REG, current_index);
    reg_write(RTC_CNTL_STORE7_REG, next_index);
  }

  if (next_index == UINT32_MAX) {
    if (current_pid == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xF610u);
    }
    if ((current != 0) && (current->state == PROCESS_STATE_RUNNING)) {
      preempt_enable();
      return;
    }

    if (process_core_count_blocked_sleepers(scheduler_runtime_states_const(), MAX_PROCESSES) != 0u) {
      for (;;) {
        __asm__ volatile("csrsi mstatus, 8");
        __asm__ volatile("wfi");
        __asm__ volatile("csrci mstatus, 8");
        scheduler_sync_ready_queue();
        next_index = scheduler_pick_next_ready();
        if (next_index != UINT32_MAX) {
          break;
        }
      }
    } else {
      reg_write(RTC_CNTL_STORE5_REG, DIAG_SCHED_OR_STACK);
      reg_write(RTC_CNTL_STORE6_REG, DIAGD_NO_RUNNABLE_TASK);
      reg_write(RTC_CNTL_STORE7_REG, current_index);
      console_log("SCHED", "no runnable processes");
      panic_halt();
    }
  }

  if (next_index == current_index) {
    if (current_pid == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xF620u | (next_index & 0xFFu));
    }
    if (current != 0) {
      current->state = PROCESS_STATE_RUNNING;
    }
    preempt_enable();
    return;
  }

  if (current_pid == 1u) {
    reg_write(RTC_CNTL_STORE0_REG, 0xF630u | (next_index & 0xFFu));
    reg_write(RTC_CNTL_STORE4_REG, g_process_exec[current_index].stack_pointer[0]);
    reg_write(RTC_CNTL_STORE5_REG, g_process_exec[next_index].stack_pointer[0]);
    reg_write(RTC_CNTL_STORE6_REG, (uint32_t)(uintptr_t)g_process_exec[current_index].stack_pointer);
    reg_write(RTC_CNTL_STORE7_REG, (uint32_t)(uintptr_t)g_process_exec[next_index].stack_pointer);
  }

  uint32_t mstatus = interrupts_save_disable();
  scheduler_validate_switch_targets(current_index, next_index);

  ++g_switch_count;
  if (g_switch_count == 1u) {
    reg_write(RTC_CNTL_STORE1_REG, g_switch_count);
    reg_write(RTC_CNTL_STORE2_REG, (current_index << 16) | next_index);
  } else if ((g_switch_count & 0x0Fu) == 0u) {
    reg_write(RTC_CNTL_STORE1_REG, g_switch_count);
    reg_write(RTC_CNTL_STORE2_REG, (current_index << 16) | next_index);
  }

  if ((g_sched_verbose_logs != 0u) &&
      ((g_switch_count <= 8u) || ((g_switch_count - g_last_switch_log_count) >= 64u))) {
    g_last_switch_log_count = g_switch_count;
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_count, g_switch_count);
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_from, current_pid);
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_to, g_processes[next_index].pid);
  }

  if ((g_switch_count <= 16u) || ((g_switch_count % 8192u) == 0u)) {
    console_log(g_sched_dbg_tag, g_sched_dbg_hw_switch);
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_count, g_switch_count);
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_from, current_pid);
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_switch_to, g_processes[next_index].pid);
  }

  if ((g_switch_count == 0x100u) && (trap_timer_interrupt_seen() == 0u)) {
    reg_write(RTC_CNTL_STORE5_REG, DIAG_PRE_TIMER_STALL);
    reg_write(RTC_CNTL_STORE6_REG, DIAGD_NO_TIMER_VECTOR_AFTER_SWITCHES);
    reg_write(RTC_CNTL_STORE7_REG, g_switch_count);
  }

  if ((g_systimer_pending_polled == 0u) && (trap_timer_interrupt_seen() == 0u)) {
    uint32_t int_raw = reg_read(SYSTIMER_INT_RAW_REG);
    uint32_t int_st = reg_read(SYSTIMER_INT_ST_REG);
    if (((int_raw | int_st) & SYSTIMER_TARGET0_INT_ST_BIT) != 0u) {
      uint32_t route = reg_read(INTERRUPT_CORE0_SYSTIMER_TARGET0_INT_MAP_REG);
      uint32_t eip = reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG);
      uint32_t enable = reg_read(INTERRUPT_CORE0_CPU_INT_ENABLE_REG);
      uint32_t type = reg_read(INTERRUPT_CORE0_CPU_INT_TYPE_REG);
      uint32_t priority = reg_read(INTERRUPT_CPU_INT_PRIO_REG(CPU_SYSTIMER_INTR_NUM));
      uint32_t snapshot = 0u;

      g_systimer_pending_polled = 1u;
      reg_write(RTC_CNTL_STORE0_REG, 0xD201u);
      reg_write(RTC_CNTL_STORE4_REG, eip);
      reg_write(RTC_CNTL_STORE5_REG, DIAG_TIMER_PATH_BAD);
      reg_write(RTC_CNTL_STORE6_REG, route);

      snapshot |= ((enable & BIT(CPU_SYSTIMER_INTR_NUM)) != 0u) ? BIT(0) : 0u;
      snapshot |= ((type & BIT(CPU_SYSTIMER_INTR_NUM)) != 0u) ? BIT(1) : 0u;
      snapshot |= (priority & 0xFFu) << 8;
      snapshot |= (int_raw & SYSTIMER_TARGET0_INT_ST_BIT) != 0u ? BIT(16) : 0u;
      snapshot |= (int_st & SYSTIMER_TARGET0_INT_ST_BIT) != 0u ? BIT(17) : 0u;
      reg_write(RTC_CNTL_STORE7_REG, snapshot);
    }
  }

  /*
   * process_yield() spans a task boundary. Keep the global safe-point disable
   * depth from leaking into the next task, otherwise the new task inherits a
   * permanently deferred preempt state.
   */
  preempt_enable();
  scheduler_switch_process(current_index, next_index);
  interrupts_restore(mstatus);
  scheduler_wait_for_interrupt_window(mstatus);
  preempt_enable();
}

void scheduler_run(void) {
  if (g_sched_verbose_logs != 0u) {
    console_log(g_sched_dbg_tag, g_sched_dbg_run);
  }
  g_current_process_index = UINT32_MAX;
  reg_write(RTC_CNTL_STORE0_REG, 0xC099u);
  process_yield();
  panic_halt();
}

void scheduler_task_bootstrap(void) {
  struct process_runtime_state *current = scheduler_current_process();
  uint32_t pid_bit;

  if ((current == 0) || (g_process_exec[g_current_process_index].entry == 0)) {
    console_log("SCHED", "bootstrap without process");
    panic_halt();
  }

  pid_bit = (current->pid > 0u) && (current->pid <= 16u) ? BIT(current->pid - 1u) : 0u;
  if ((pid_bit != 0u) && ((g_bootstrap_seen_mask & pid_bit) == 0u)) {
    g_bootstrap_seen_mask |= (uint16_t)pid_bit;
    reg_write(RTC_CNTL_STORE0_REG, 0xF400u | (current->pid & 0xFFu));
    reg_write(RTC_CNTL_STORE1_REG, current->pid);
    reg_write(RTC_CNTL_STORE2_REG, g_current_process_index);
    reg_write(RTC_CNTL_STORE5_REG, g_bootstrap_seen_mask);
#if !CONFIG_SCHEDULER_PREEMPTIVE
    console_log_u32(g_sched_dbg_tag, g_sched_dbg_bootstrap_pid, current->pid);
    if (g_sched_verbose_logs != 0u) {
      console_log(g_sched_dbg_tag, g_sched_dbg_bootstrap);
    }
#endif
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xF420u | (current->pid & 0xFFu));
  reg_write(RTC_CNTL_STORE1_REG, current->pid);
  g_process_exec[g_current_process_index].entry();
  process_exit(0);
}

const char *scheduler_current_task_name(void) {
  struct process_runtime_state *current = scheduler_current_process();
  return (current != 0) ? current->name : "idle";
}

uint32_t scheduler_switch_count_read(void) {
  return g_switch_count;
}

void task_create(const char *name, task_entry_t entry) {
  if (process_spawn(name, entry) == 0u) {
    console_log("SCHED", "task_create failed");
    panic_halt();
  }
}

void task_yield(void) {
  process_yield();
}

void task_exit(void) {
  process_exit(0);
}
