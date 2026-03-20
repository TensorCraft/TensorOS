#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

#define RUNTIME_UI_EVENT_QUEUE_CAPACITY 16u
#define RUNTIME_UI_TIMER_CAPACITY 8u

enum runtime_ui_event_type {
  RUNTIME_UI_EVENT_NONE = 0u,
  RUNTIME_UI_EVENT_INPUT_KEY = 1u,
  RUNTIME_UI_EVENT_INPUT_POINTER_MOVE = 2u,
  RUNTIME_UI_EVENT_INPUT_POINTER_BUTTON = 3u,
  RUNTIME_UI_EVENT_FOCUS = 4u,
  RUNTIME_UI_EVENT_TIMER = 5u
};

struct runtime_ui_event {
  uint32_t type;
  uint32_t target_pid;
  uint32_t tick;
  uint32_t arg0;
  uint32_t arg1;
  uint32_t arg2;
};

struct runtime_ui_event_queue {
  struct runtime_ui_event events[RUNTIME_UI_EVENT_QUEUE_CAPACITY];
  uint32_t read_index;
  uint32_t count;
};

struct runtime_ui_timer_entry {
  uint32_t owner_pid;
  uint32_t due_tick;
  uint32_t token;
  uint8_t active;
};

struct runtime_ui_timer_table {
  struct runtime_ui_timer_entry entries[RUNTIME_UI_TIMER_CAPACITY];
};

void runtime_ui_event_queue_init(struct runtime_ui_event_queue *queue);
int32_t runtime_ui_event_push(struct runtime_ui_event_queue *queue,
                              const struct runtime_ui_event *event);
int32_t runtime_ui_event_peek(const struct runtime_ui_event_queue *queue,
                              struct runtime_ui_event *event);
int32_t runtime_ui_event_pop(struct runtime_ui_event_queue *queue, struct runtime_ui_event *event);

void runtime_ui_timer_table_init(struct runtime_ui_timer_table *table);
int32_t runtime_ui_timer_arm(struct runtime_ui_timer_table *table, uint32_t owner_pid,
                             uint32_t due_tick, uint32_t token);
int32_t runtime_ui_timer_cancel(struct runtime_ui_timer_table *table, uint32_t owner_pid,
                                uint32_t token);
int32_t runtime_ui_timer_next_due(const struct runtime_ui_timer_table *table, uint32_t *due_tick);
uint32_t runtime_ui_timer_collect_due(struct runtime_ui_timer_table *table, uint32_t current_tick,
                                      struct runtime_ui_event_queue *queue);
