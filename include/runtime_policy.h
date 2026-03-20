#pragma once

#include "process_model.h"

#include <stdint.h>

int runtime_policy_can_spawn(const struct process_runtime_state *processes, uint32_t capacity,
                             uint32_t role);
uint32_t runtime_policy_count_role(const struct process_runtime_state *processes, uint32_t capacity,
                                   uint32_t role);
const char *scheduler_mode_name(void);
const char *runtime_policy_mode_name(void);
const char *runtime_process_role_name(uint32_t role);
