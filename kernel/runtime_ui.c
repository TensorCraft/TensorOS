#include "runtime_ui.h"

static uint32_t runtime_ui_event_queue_write_index(const struct runtime_ui_event_queue *queue) {
  return (queue->read_index + queue->count) % RUNTIME_UI_EVENT_QUEUE_CAPACITY;
}

void runtime_ui_event_queue_init(struct runtime_ui_event_queue *queue) {
  if (queue == 0) {
    return;
  }

  queue->read_index = 0u;
  queue->count = 0u;
}

int32_t runtime_ui_event_push(struct runtime_ui_event_queue *queue,
                              const struct runtime_ui_event *event) {
  uint32_t write_index;

  if ((queue == 0) || (event == 0) || (event->type == RUNTIME_UI_EVENT_NONE)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (queue->count >= RUNTIME_UI_EVENT_QUEUE_CAPACITY) {
    return RUNTIME_SYSCALL_STATUS_ENOSPC;
  }

  write_index = runtime_ui_event_queue_write_index(queue);
  queue->events[write_index] = *event;
  ++queue->count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_ui_event_peek(const struct runtime_ui_event_queue *queue,
                              struct runtime_ui_event *event) {
  if ((queue == 0) || (event == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  if (queue->count == 0u) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  *event = queue->events[queue->read_index];
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t runtime_ui_event_pop(struct runtime_ui_event_queue *queue, struct runtime_ui_event *event) {
  int32_t status;

  status = runtime_ui_event_peek(queue, event);
  if (status != RUNTIME_SYSCALL_STATUS_OK) {
    return status;
  }

  queue->read_index = (queue->read_index + 1u) % RUNTIME_UI_EVENT_QUEUE_CAPACITY;
  --queue->count;
  return RUNTIME_SYSCALL_STATUS_OK;
}

void runtime_ui_timer_table_init(struct runtime_ui_timer_table *table) {
  if (table == 0) {
    return;
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    table->entries[index].owner_pid = 0u;
    table->entries[index].due_tick = 0u;
    table->entries[index].token = 0u;
    table->entries[index].active = 0u;
  }
}

int32_t runtime_ui_timer_arm(struct runtime_ui_timer_table *table, uint32_t owner_pid,
                             uint32_t due_tick, uint32_t token) {
  if ((table == 0) || (owner_pid == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    if ((table->entries[index].active != 0u) && (table->entries[index].owner_pid == owner_pid) &&
        (table->entries[index].token == token)) {
      table->entries[index].due_tick = due_tick;
      return RUNTIME_SYSCALL_STATUS_OK;
    }
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    if (table->entries[index].active != 0u) {
      continue;
    }

    table->entries[index].owner_pid = owner_pid;
    table->entries[index].due_tick = due_tick;
    table->entries[index].token = token;
    table->entries[index].active = 1u;
    return RUNTIME_SYSCALL_STATUS_OK;
  }

  return RUNTIME_SYSCALL_STATUS_ENOSPC;
}

int32_t runtime_ui_timer_cancel(struct runtime_ui_timer_table *table, uint32_t owner_pid,
                                uint32_t token) {
  if ((table == 0) || (owner_pid == 0u)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    if ((table->entries[index].active != 0u) && (table->entries[index].owner_pid == owner_pid) &&
        (table->entries[index].token == token)) {
      table->entries[index].active = 0u;
      table->entries[index].owner_pid = 0u;
      table->entries[index].due_tick = 0u;
      table->entries[index].token = 0u;
      return RUNTIME_SYSCALL_STATUS_OK;
    }
  }

  return RUNTIME_SYSCALL_STATUS_ENOENT;
}

int32_t runtime_ui_timer_next_due(const struct runtime_ui_timer_table *table, uint32_t *due_tick) {
  uint32_t found = 0u;
  uint32_t best_due = 0u;

  if ((table == 0) || (due_tick == 0)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    if (table->entries[index].active == 0u) {
      continue;
    }

    if ((found == 0u) || (table->entries[index].due_tick < best_due)) {
      best_due = table->entries[index].due_tick;
      found = 1u;
    }
  }

  if (found == 0u) {
    return RUNTIME_SYSCALL_STATUS_ENOENT;
  }

  *due_tick = best_due;
  return RUNTIME_SYSCALL_STATUS_OK;
}

uint32_t runtime_ui_timer_collect_due(struct runtime_ui_timer_table *table, uint32_t current_tick,
                                      struct runtime_ui_event_queue *queue) {
  uint32_t produced = 0u;

  if ((table == 0) || (queue == 0)) {
    return 0u;
  }

  for (uint32_t index = 0u; index < RUNTIME_UI_TIMER_CAPACITY; ++index) {
    struct runtime_ui_event event;

    if ((table->entries[index].active == 0u) || (table->entries[index].due_tick > current_tick)) {
      continue;
    }

    event.type = RUNTIME_UI_EVENT_TIMER;
    event.target_pid = table->entries[index].owner_pid;
    event.tick = current_tick;
    event.arg0 = table->entries[index].token;
    event.arg1 = table->entries[index].due_tick;
    event.arg2 = 0u;
    if (runtime_ui_event_push(queue, &event) != RUNTIME_SYSCALL_STATUS_OK) {
      break;
    }

    table->entries[index].active = 0u;
    table->entries[index].owner_pid = 0u;
    table->entries[index].due_tick = 0u;
    table->entries[index].token = 0u;
    ++produced;
  }

  return produced;
}
