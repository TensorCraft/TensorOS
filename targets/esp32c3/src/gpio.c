#include "esp32c3_gpio.h"
#include "soc.h"

struct esp32c3_gpio_interrupt_slot {
  esp32c3_gpio_interrupt_handler_t handler;
  void *context;
};

static struct esp32c3_gpio_interrupt_slot g_interrupt_slots[22];

static void esp32c3_gpio_select_gpio_func(uint32_t pin, int pullup) {
  uint32_t mux_reg;
  uint32_t mux_value;

  if (pin > 21u) {
    return;
  }

  mux_reg = IO_MUX_GPIO_REG(pin);
  mux_value = reg_read(mux_reg);
  mux_value &= ~(IO_MUX_MCU_SEL_MASK | IO_MUX_FUN_PD_BIT);
  mux_value |= IO_MUX_FUN_IE_BIT | PIN_FUNC_GPIO_VALUE;
  if (pullup) {
    mux_value |= IO_MUX_FUN_PU_BIT;
  } else {
    mux_value &= ~IO_MUX_FUN_PU_BIT;
  }
  reg_write(mux_reg, mux_value);
  reg_write(GPIO_FUNC_OUT_SEL_REG(pin), GPIO_SIG_GPIO_OUT);
}

void esp32c3_gpio_config_output_function(uint32_t pin, uint32_t function_value, uint32_t signal_idx) {
  uint32_t mux_reg;
  uint32_t mux_value;

  (void)function_value;

  if (pin > 21u) {
    return;
  }

  mux_reg = IO_MUX_GPIO_REG(pin);
  mux_value = reg_read(mux_reg);
  mux_value &= ~(IO_MUX_MCU_SEL_MASK | IO_MUX_FUN_PD_BIT | IO_MUX_FUN_PU_BIT);
  mux_value |= PIN_FUNC_GPIO_VALUE;
  reg_write(mux_reg, mux_value);
  reg_write(GPIO_FUNC_OUT_SEL_REG(pin), signal_idx);
  esp32c3_gpio_enable_output(pin);
}

void esp32c3_gpio_config_input(uint32_t pin, int pullup) {
  esp32c3_gpio_select_gpio_func(pin, pullup);
  esp32c3_gpio_disable_output(pin);
}

void esp32c3_gpio_config_output(uint32_t pin) {
  esp32c3_gpio_select_gpio_func(pin, 0);
  esp32c3_gpio_enable_output(pin);
}

void esp32c3_gpio_enable_output(uint32_t pin) {
  if (pin > 21u) {
    return;
  }

  reg_write(GPIO_ENABLE_W1TS_REG, BIT(pin));
}

void esp32c3_gpio_disable_output(uint32_t pin) {
  if (pin > 21u) {
    return;
  }

  reg_write(GPIO_ENABLE_W1TC_REG, BIT(pin));
}

void esp32c3_gpio_set_level(uint32_t pin, int level) {
  if (pin > 21u) {
    return;
  }

  if (level) {
    reg_write(GPIO_OUT_W1TS_REG, BIT(pin));
  } else {
    reg_write(GPIO_OUT_W1TC_REG, BIT(pin));
  }
}

int esp32c3_gpio_get_level(uint32_t pin) {
  if (pin > 21u) {
    return 0;
  }

  return (reg_read(GPIO_IN_REG) & BIT(pin)) != 0u;
}

void esp32c3_gpio_interrupt_set_handler(uint32_t pin, esp32c3_gpio_interrupt_handler_t handler,
                                        void *context) {
  if (pin > 21u) {
    return;
  }

  g_interrupt_slots[pin].handler = handler;
  g_interrupt_slots[pin].context = context;
}

void esp32c3_gpio_interrupt_config(uint32_t pin, uint32_t interrupt_type) {
  uint32_t pin_reg;
  uint32_t pin_value;

  if (pin > 21u) {
    return;
  }

  pin_reg = GPIO_PIN_REG(pin);
  pin_value = reg_read(pin_reg);
  pin_value &= ~(GPIO_PIN_INT_ENA_MASK | GPIO_PIN_INT_TYPE_MASK);
  pin_value |= ((interrupt_type << GPIO_PIN_INT_TYPE_SHIFT) & GPIO_PIN_INT_TYPE_MASK);
  if (interrupt_type != ESP32C3_GPIO_INTERRUPT_DISABLE) {
    pin_value |= ((GPIO_PRO_CPU_INTR_ENA_BIT << GPIO_PIN_INT_ENA_SHIFT) & GPIO_PIN_INT_ENA_MASK);
  }
  reg_write(pin_reg, pin_value);
  esp32c3_gpio_interrupt_clear(pin);
}

int esp32c3_gpio_interrupt_pending(uint32_t pin) {
  if (pin > 21u) {
    return 0;
  }

  return (reg_read(GPIO_STATUS_REG) & BIT(pin)) != 0u;
}

void esp32c3_gpio_interrupt_clear(uint32_t pin) {
  if (pin > 21u) {
    return;
  }

  reg_write(GPIO_STATUS_W1TC_REG, BIT(pin));
}

void esp32c3_gpio_interrupt_dispatch(uint32_t pending_mask) {
  uint32_t mask = pending_mask & 0x003FFFFFu;

  while (mask != 0u) {
    uint32_t pin = 0u;
    while (((mask >> pin) & 0x1u) == 0u) {
      ++pin;
    }

    if (pin <= 21u) {
      esp32c3_gpio_interrupt_handler_t handler = g_interrupt_slots[pin].handler;
      if (handler != 0) {
        handler(pin, g_interrupt_slots[pin].context);
      }
    }
    mask &= ~BIT(pin);
  }
}

void esp32c3_gpio_delay_cycles(uint32_t cycles) {
  while (cycles-- != 0u) {
    __asm__ volatile("nop");
  }
}

void esp32c3_gpio_delay_ms(uint32_t milliseconds) {
  while (milliseconds-- != 0u) {
    esp32c3_gpio_delay_cycles(15000u);
  }
}
