#pragma once

#include "runtime_config.h"

#include <stdint.h>

typedef void (*task_entry_t)(void);
typedef uint32_t process_id_t;
struct kernel_wait_queue;
struct kernel_event;
struct kernel_semaphore;
struct kernel_mutex;
struct kernel_mailbox;

enum process_state {
  PROCESS_STATE_UNUSED = 0u,
  PROCESS_STATE_READY = 1u,
  PROCESS_STATE_RUNNING = 2u,
  PROCESS_STATE_BLOCKED = 3u,
  PROCESS_STATE_ZOMBIE = 4u
};

enum process_wait_reason {
  PROCESS_WAIT_NONE = 0u,
  PROCESS_WAIT_SLEEP_TICKS = 1u,
  PROCESS_WAIT_MANUAL = 2u,
  PROCESS_WAIT_CHILD = 3u,
  PROCESS_WAIT_EVENT_CHANNEL = 4u
};

enum runtime_process_role {
  RUNTIME_PROCESS_ROLE_NONE = 0u,
  RUNTIME_PROCESS_ROLE_SYSTEM = 1u,
  RUNTIME_PROCESS_ROLE_FOREGROUND_APP = 2u,
  RUNTIME_PROCESS_ROLE_BACKGROUND_APP = 3u,
  RUNTIME_PROCESS_ROLE_LIVE_ACTIVITY = 4u
};

struct process_info {
  process_id_t pid;
  process_id_t parent_pid;
  const char *name;
  uint32_t role;
  uint32_t state;
  int32_t exit_code;
  uint32_t switch_count;
  uint32_t yield_count;
  uint64_t wake_tick;
  uint32_t wait_reason;
  uint32_t wait_channel;
};

struct kmem_stats {
  uint32_t arena_capacity_bytes;
  uint32_t free_bytes;
  uint32_t largest_free_block;
  uint32_t bytes_in_use;
  uint32_t peak_bytes_in_use;
  uint32_t allocation_count;
  uint32_t free_count;
  uint32_t allocation_fail_count;
  uint32_t live_allocations;
  uint32_t peak_live_allocations;
  uint32_t block_count;
  uint32_t free_block_count;
  uint32_t used_block_count;
};

enum diagnostic_reason {
  DIAG_NONE = 0u,
  DIAG_PRE_TIMER_STALL = 1u,
  DIAG_TIMER_PATH_BAD = 2u,
  DIAG_POST_TIMER_STALL = 3u,
  DIAG_TRAP_EXCEPTION = 4u,
  DIAG_SCHED_OR_STACK = 5u
};

enum diagnostic_detail {
  DIAGD_NONE = 0u,
  DIAGD_NO_TIMER_VECTOR_AFTER_SWITCHES = 0x0101u,
  DIAGD_TIMER_VECTOR_BUT_ST0 = 0x0201u,
  DIAGD_TIMER_RAW1_ST0 = 0x0202u,
  DIAGD_TICK_SEEN_BUT_SWITCH_STOPPED = 0x0301u,
  DIAGD_BAD_CURRENT_INDEX = 0x0501u,
  DIAGD_BAD_NEXT_INDEX = 0x0502u,
  DIAGD_NO_RUNNABLE_TASK = 0x0503u,
  DIAGD_BAD_TASK_SP_RANGE = 0x0504u,
  DIAGD_BAD_TASK_SP_ALIGN = 0x0505u,
  DIAGD_UNEXPECTED_INTERRUPT = 0x0601u,
  DIAGD_EXCEPTION_MCAUSE = 0x0400u
};

void boot_main(void);
void kernel_main(void);

void trap_init(void);
uint32_t trap_mtvec_read(void);
void trap_interrupt_c(uint32_t mcause);
void trap_exception_c(uint32_t mcause, uint32_t mepc, uint32_t mtval);
void panic_halt(void);
void interrupts_init(void);
void interrupts_init_software_test(void);
void interrupts_enable(void);
void interrupts_ack(uint32_t cpu_interrupt_number);
void interrupts_trigger_software_test(void);
void interrupts_trigger_line17_probe(void);

void console_init(void);
void console_putc(char c);
void console_write(const char *s);
void console_puts(const char *s);
void console_puthex32(uint32_t value);
void console_putdec_u32(uint32_t value);
void console_log(const char *tag, const char *message);
void console_log_u32(const char *tag, const char *label, uint32_t value);
void console_log_hex32(const char *tag, const char *label, uint32_t value);

void uart0_init(void);
void uart0_putc(char c);
void uart0_puts(const char *s);
void uart0_puthex32(uint32_t value);
void uart0_putdec_u32(uint32_t value);
int uart0_try_getc(char *c);

void usb_serial_jtag_init(void);
void usb_serial_jtag_putc(char c);
void usb_serial_jtag_flush(void);

void systimer_unit0_init(void);
uint64_t systimer_unit0_read(void);
int systimer_unit0_is_initialized(void);
void systimer_tick_init(void);
void systimer_tick_isr(void);
uint64_t kernel_ticks_read(void);
void systimer_tick_debug_dump(void);
void process_tick(uint64_t tick_count);

void kmem_init(void);
void *kmem_alloc(uint32_t size);
void kmem_free(void *ptr);
uint32_t kmem_free_bytes(void);
uint32_t kmem_largest_free_block(void);
uint32_t kmem_bytes_in_use(void);
uint32_t kmem_peak_bytes_in_use(void);
uint32_t kmem_allocation_count(void);
uint32_t kmem_free_count(void);
uint32_t kmem_allocation_fail_count(void);
uint32_t kmem_live_allocations(void);
uint32_t kmem_peak_live_allocations(void);
void kmem_stats_snapshot(struct kmem_stats *stats);

process_id_t process_spawn(const char *name, task_entry_t entry);
process_id_t process_spawn_with_role(const char *name, task_entry_t entry, uint32_t role);
void process_yield(void);
void process_exit(int32_t exit_code);
void process_sleep(uint32_t ticks);
int process_block(process_id_t pid);
int process_wake(process_id_t pid);
void process_wait_channel(uint32_t channel);
uint32_t process_wake_first_channel(uint32_t channel);
uint32_t process_wake_channel(uint32_t channel);
int process_kill(process_id_t pid, int32_t exit_code);
int process_reap(process_id_t pid, int32_t *exit_code);
int process_wait(int32_t *exit_code);
int process_waitpid(process_id_t pid, int32_t *exit_code);
process_id_t process_current_pid(void);
uint32_t process_count(void);
uint32_t process_snapshot(struct process_info *buffer, uint32_t capacity);
const char *runtime_process_role_name(uint32_t role);
const char *runtime_policy_mode_name(void);
const char *scheduler_mode_name(void);
void scheduler_preempt_init(void);
void scheduler_preempt_on_timer_tick(void);
int scheduler_preempt_supported(void);
int scheduler_preempt_target_capable(void);
int scheduler_preempt_pending(void);
void scheduler_preempt_clear_pending(void);
int scheduler_maybe_preempt_at_safe_point(void);
void preempt_disable(void);
void preempt_enable(void);
uint32_t preempt_disable_depth(void);
uint32_t scheduler_preempt_tick_count(void);
const char *scheduler_preempt_status_name(void);

void task_create(const char *name, task_entry_t entry);
void task_yield(void);
void scheduler_run(void);
void scheduler_task_bootstrap(void);
const char *scheduler_current_task_name(void);
uint32_t scheduler_switch_count_read(void);
void context_switch(uint32_t **old_sp, uint32_t *new_sp);
void task_start_first(uint32_t *new_sp);
void task_exit(void);
uint32_t trap_timer_interrupt_seen(void);
void runtime_poll_timer_delivery(void);

struct kernel_wait_queue *kernel_wait_queue_create(void);
int kernel_wait_queue_destroy(struct kernel_wait_queue *wait_queue);
void kernel_wait_queue_wait(struct kernel_wait_queue *wait_queue);
uint32_t kernel_wait_queue_wake_one(struct kernel_wait_queue *wait_queue);
uint32_t kernel_wait_queue_wake_all(struct kernel_wait_queue *wait_queue);
uint32_t kernel_wait_queue_waiter_count(const struct kernel_wait_queue *wait_queue);

struct kernel_event *kernel_event_create(void);
int kernel_event_destroy(struct kernel_event *event);
void kernel_event_wait(struct kernel_event *event);
uint32_t kernel_event_signal(struct kernel_event *event);
uint32_t kernel_event_waiter_count(const struct kernel_event *event);

struct kernel_semaphore *kernel_semaphore_create(uint32_t initial_count, uint32_t max_count);
int kernel_semaphore_destroy(struct kernel_semaphore *semaphore);
int kernel_semaphore_try_acquire(struct kernel_semaphore *semaphore);
void kernel_semaphore_acquire(struct kernel_semaphore *semaphore);
int kernel_semaphore_release(struct kernel_semaphore *semaphore);
uint32_t kernel_semaphore_count(const struct kernel_semaphore *semaphore);
uint32_t kernel_semaphore_waiter_count(const struct kernel_semaphore *semaphore);

struct kernel_mutex *kernel_mutex_create(void);
int kernel_mutex_destroy(struct kernel_mutex *mutex);
int kernel_mutex_try_lock(struct kernel_mutex *mutex);
void kernel_mutex_lock(struct kernel_mutex *mutex);
int kernel_mutex_unlock(struct kernel_mutex *mutex);
process_id_t kernel_mutex_owner_pid(const struct kernel_mutex *mutex);
uint32_t kernel_mutex_waiter_count(const struct kernel_mutex *mutex);

struct kernel_mailbox *kernel_mailbox_create(void);
int kernel_mailbox_destroy(struct kernel_mailbox *mailbox);
int kernel_mailbox_try_send(struct kernel_mailbox *mailbox, uint32_t message);
void kernel_mailbox_send(struct kernel_mailbox *mailbox, uint32_t message);
int kernel_mailbox_try_receive(struct kernel_mailbox *mailbox, uint32_t *message);
uint32_t kernel_mailbox_receive(struct kernel_mailbox *mailbox);
int kernel_mailbox_has_message(const struct kernel_mailbox *mailbox);
uint32_t kernel_mailbox_waiting_senders(const struct kernel_mailbox *mailbox);
uint32_t kernel_mailbox_waiting_receivers(const struct kernel_mailbox *mailbox);

extern char __trap_entry[];
extern char __mtvec_base[];
