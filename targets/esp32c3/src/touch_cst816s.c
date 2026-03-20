#include "esp32c3_touch_cst816s.h"
#include "esp32c3_gpio.h"

enum {
  CST816S_PIN_SDA = 4,
  CST816S_PIN_SCL = 5,
  CST816S_PIN_INT = ESP32C3_TOUCH_CST816S_IRQ_PIN,
  CST816S_I2C_ADDRESS = 0x15,
};

static volatile uint32_t g_cst816s_irq_count;
static volatile uint8_t g_cst816s_irq_pending;

static void cst816s_irq_handler(uint32_t pin, void *context) {
  (void)pin;
  (void)context;
  ++g_cst816s_irq_count;
  g_cst816s_irq_pending = 1u;
}

static void cst816s_i2c_delay(void) { esp32c3_gpio_delay_cycles(40u); }

static void cst816s_sda_release(void) {
  esp32c3_gpio_config_input(CST816S_PIN_SDA, 1);
  esp32c3_gpio_disable_output(CST816S_PIN_SDA);
}

static void cst816s_scl_release(void) {
  esp32c3_gpio_config_input(CST816S_PIN_SCL, 1);
  esp32c3_gpio_disable_output(CST816S_PIN_SCL);
}

static void cst816s_sda_low(void) {
  esp32c3_gpio_config_output(CST816S_PIN_SDA);
  esp32c3_gpio_set_level(CST816S_PIN_SDA, 0);
}

static void cst816s_scl_low(void) {
  esp32c3_gpio_config_output(CST816S_PIN_SCL);
  esp32c3_gpio_set_level(CST816S_PIN_SCL, 0);
}

static int cst816s_sda_read(void) { return esp32c3_gpio_get_level(CST816S_PIN_SDA); }

static void cst816s_i2c_start(void) {
  cst816s_sda_release();
  cst816s_scl_release();
  cst816s_i2c_delay();
  cst816s_sda_low();
  cst816s_i2c_delay();
  cst816s_scl_low();
}

static void cst816s_i2c_stop(void) {
  cst816s_sda_low();
  cst816s_i2c_delay();
  cst816s_scl_release();
  cst816s_i2c_delay();
  cst816s_sda_release();
  cst816s_i2c_delay();
}

static int cst816s_i2c_write_byte(uint8_t value) {
  for (uint32_t bit = 0u; bit < 8u; ++bit) {
    if ((value & 0x80u) != 0u) {
      cst816s_sda_release();
    } else {
      cst816s_sda_low();
    }
    cst816s_i2c_delay();
    cst816s_scl_release();
    cst816s_i2c_delay();
    cst816s_scl_low();
    value <<= 1;
  }

  cst816s_sda_release();
  cst816s_i2c_delay();
  cst816s_scl_release();
  cst816s_i2c_delay();
  {
    int ack = cst816s_sda_read() == 0;
    cst816s_scl_low();
    return ack;
  }
}

static uint8_t cst816s_i2c_read_byte(int ack) {
  uint8_t value = 0u;

  cst816s_sda_release();
  for (uint32_t bit = 0u; bit < 8u; ++bit) {
    value <<= 1;
    cst816s_scl_release();
    cst816s_i2c_delay();
    if (cst816s_sda_read()) {
      value |= 1u;
    }
    cst816s_scl_low();
    cst816s_i2c_delay();
  }

  if (ack) {
    cst816s_sda_low();
  } else {
    cst816s_sda_release();
  }
  cst816s_i2c_delay();
  cst816s_scl_release();
  cst816s_i2c_delay();
  cst816s_scl_low();
  cst816s_sda_release();
  return value;
}

static int cst816s_write_then_read(uint8_t reg, uint8_t *buffer, uint32_t size) {
  if ((buffer == 0) || (size == 0u)) {
    return 0;
  }

  cst816s_i2c_start();
  if (!cst816s_i2c_write_byte((uint8_t)((CST816S_I2C_ADDRESS << 1) | 0u)) ||
      !cst816s_i2c_write_byte(reg)) {
    cst816s_i2c_stop();
    return 0;
  }

  cst816s_i2c_start();
  if (!cst816s_i2c_write_byte((uint8_t)((CST816S_I2C_ADDRESS << 1) | 1u))) {
    cst816s_i2c_stop();
    return 0;
  }

  for (uint32_t index = 0u; index < size; ++index) {
    buffer[index] = cst816s_i2c_read_byte(index + 1u < size);
  }
  cst816s_i2c_stop();
  return 1;
}

void esp32c3_touch_cst816s_init(void) {
  cst816s_sda_release();
  cst816s_scl_release();
  esp32c3_gpio_config_input(CST816S_PIN_INT, 1);
  esp32c3_gpio_interrupt_set_handler(CST816S_PIN_INT, cst816s_irq_handler, 0);
  esp32c3_gpio_interrupt_config(CST816S_PIN_INT, ESP32C3_GPIO_INTERRUPT_NEGEDGE);
  g_cst816s_irq_count = 0u;
  g_cst816s_irq_pending = 0u;
}

int32_t esp32c3_touch_cst816s_read(struct esp32c3_touch_cst816s_sample *sample) {
  uint8_t status = 0u;
  uint8_t data[4];

  if (sample == 0) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  sample->x = 0u;
  sample->y = 0u;
  sample->touching = 0u;

  if (!cst816s_write_then_read(0x02u, &status, 1u)) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }
  if (status == 0u) {
    return RUNTIME_SYSCALL_STATUS_OK;
  }
  if (!cst816s_write_then_read(0x03u, data, sizeof(data))) {
    return RUNTIME_SYSCALL_STATUS_EBUSY;
  }

  sample->x = (uint16_t)(((uint16_t)(data[0] & 0x0Fu) << 8) | data[1]);
  sample->y = (uint16_t)(((uint16_t)(data[2] & 0x0Fu) << 8) | data[3]);
  sample->touching = 1u;
  return RUNTIME_SYSCALL_STATUS_OK;
}

uint32_t esp32c3_touch_cst816s_irq_count(void) { return g_cst816s_irq_count; }

int esp32c3_touch_cst816s_consume_irq_pending(void) {
  int pending = g_cst816s_irq_pending != 0u;
  g_cst816s_irq_pending = 0u;
  return pending;
}
