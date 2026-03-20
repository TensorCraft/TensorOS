#pragma once

#include "process_model.h"
#include "runtime_policy.h"

#include <stdint.h>

uint32_t process_core_find_free_slot(const struct process_runtime_state *processes, uint32_t capacity);
uint32_t process_core_find_index_by_pid(const struct process_runtime_state *processes, uint32_t capacity,
                                        process_id_t pid);
uint32_t process_core_find_next_runnable(const struct process_runtime_state *processes, uint32_t capacity,
                                         uint32_t current_index);
uint32_t process_core_count_blocked_sleepers(const struct process_runtime_state *processes,
                                             uint32_t capacity);
void process_core_reset_slot(struct process_runtime_state *process);
void process_core_make_ready(struct process_runtime_state *process);
void process_core_block(struct process_runtime_state *process, uint32_t wait_reason,
                        uint32_t wait_channel, uint64_t wake_tick,
                        process_id_t wait_target_pid);
uint32_t process_core_find_waitable_child(const struct process_runtime_state *processes,
                                          uint32_t capacity, process_id_t parent_pid,
                                          process_id_t target_pid);
int process_core_has_child(const struct process_runtime_state *processes, uint32_t capacity,
                           process_id_t parent_pid, process_id_t target_pid);
uint32_t process_core_wake_waiting_parent(struct process_runtime_state *processes, uint32_t capacity,
                                          process_id_t child_pid);
uint32_t process_core_wake_sleepers(struct process_runtime_state *processes, uint32_t capacity,
                                    uint64_t tick_count);
uint32_t process_core_wake_channel(struct process_runtime_state *processes, uint32_t capacity,
                                   uint32_t channel);
uint32_t process_core_wake_first_channel(struct process_runtime_state *processes, uint32_t capacity,
                                         uint32_t channel, uint32_t current_index);
uint32_t process_core_snapshot(const struct process_runtime_state *processes, uint32_t capacity,
                               struct process_info *buffer, uint32_t buffer_capacity);
