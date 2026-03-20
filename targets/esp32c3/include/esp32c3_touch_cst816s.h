#pragma once

#include "runtime_syscall.h"

#include <stdint.h>

#define ESP32C3_TOUCH_CST816S_IRQ_PIN 8u

struct esp32c3_touch_cst816s_sample {
  uint16_t x;
  uint16_t y;
  uint8_t touching;
};

void esp32c3_touch_cst816s_init(void);
int32_t esp32c3_touch_cst816s_read(struct esp32c3_touch_cst816s_sample *sample);
uint32_t esp32c3_touch_cst816s_irq_count(void);
int esp32c3_touch_cst816s_consume_irq_pending(void);
