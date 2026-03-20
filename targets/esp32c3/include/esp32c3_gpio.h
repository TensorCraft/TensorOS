#pragma once

#include <stdint.h>

enum esp32c3_gpio_interrupt_type {
  ESP32C3_GPIO_INTERRUPT_DISABLE = 0u,
  ESP32C3_GPIO_INTERRUPT_POSEDGE = 1u,
  ESP32C3_GPIO_INTERRUPT_NEGEDGE = 2u,
  ESP32C3_GPIO_INTERRUPT_ANYEDGE = 3u,
  ESP32C3_GPIO_INTERRUPT_LOW_LEVEL = 4u,
  ESP32C3_GPIO_INTERRUPT_HIGH_LEVEL = 5u,
};

typedef void (*esp32c3_gpio_interrupt_handler_t)(uint32_t pin, void *context);

void esp32c3_gpio_config_input(uint32_t pin, int pullup);
void esp32c3_gpio_config_output(uint32_t pin);
void esp32c3_gpio_config_output_function(uint32_t pin, uint32_t function_value, uint32_t signal_idx);
void esp32c3_gpio_enable_output(uint32_t pin);
void esp32c3_gpio_disable_output(uint32_t pin);
void esp32c3_gpio_set_level(uint32_t pin, int level);
int esp32c3_gpio_get_level(uint32_t pin);
void esp32c3_gpio_interrupt_set_handler(uint32_t pin, esp32c3_gpio_interrupt_handler_t handler,
                                        void *context);
void esp32c3_gpio_interrupt_config(uint32_t pin, uint32_t interrupt_type);
int esp32c3_gpio_interrupt_pending(uint32_t pin);
void esp32c3_gpio_interrupt_clear(uint32_t pin);
void esp32c3_gpio_interrupt_dispatch(uint32_t pending_mask);
void esp32c3_gpio_delay_cycles(uint32_t cycles);
void esp32c3_gpio_delay_ms(uint32_t milliseconds);
