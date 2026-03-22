#include "esp32c3_panel_gc9a01.h"
#include "esp32c3_gpio.h"
#include "soc.h"

#include <stddef.h>

enum {
  GC9A01_PIN_RST = 1,
  GC9A01_PIN_DC = 2,
  GC9A01_PIN_BL = 3,
  GC9A01_PIN_SCK = 6,
  GC9A01_PIN_MOSI = 7,
  GC9A01_PIN_CS = 10,
};

enum {
  GC9A01_SIGNAL_SETTLE_CYCLES = 32u,
  GC9A01_SPI_MAX_TRANSFER_BYTES = 64u,
  GC9A01_RAMWR_TO_DATA_CYCLES = 0u,
};

#ifndef GC9A01_MADCTL_VALUE
#define GC9A01_MADCTL_VALUE 0x48u
#endif

struct gc9a01_init_cmd {
  uint8_t command;
  const uint8_t *data;
  uint8_t data_len;
  uint16_t delay_ms;
};

static uint16_t g_gc9a01_line_buffer[ESP32C3_PANEL_GC9A01_WIDTH];

enum {
  GC9A01_BL_LEDC_DUTY_RES = 10u,
  GC9A01_BL_LEDC_MAX_DUTY = (1u << GC9A01_BL_LEDC_DUTY_RES) - 1u,
  GC9A01_BL_LEDC_DUTY = GC9A01_BL_LEDC_MAX_DUTY / 5u,
  GC9A01_BL_LEDC_DIV = 4000u,
};

static uint8_t gc9a01_d_eb_14[] = {0x14};
static uint8_t gc9a01_d_84[] = {0x40};
static uint8_t gc9a01_d_85[] = {0xFF};
static uint8_t gc9a01_d_86[] = {0xFF};
static uint8_t gc9a01_d_87[] = {0xFF};
static uint8_t gc9a01_d_88[] = {0x0A};
static uint8_t gc9a01_d_89[] = {0x21};
static uint8_t gc9a01_d_8a[] = {0x00};
static uint8_t gc9a01_d_8b[] = {0x80};
static uint8_t gc9a01_d_8c[] = {0x01};
static uint8_t gc9a01_d_8d[] = {0x01};
static uint8_t gc9a01_d_8e[] = {0xFF};
static uint8_t gc9a01_d_8f[] = {0xFF};
static uint8_t gc9a01_d_b6[] = {0x00, 0x20};
static uint8_t gc9a01_d_3a[] = {0x05};
static uint8_t gc9a01_d_90[] = {0x08, 0x08, 0x08, 0x08};
static uint8_t gc9a01_d_bd[] = {0x06};
static uint8_t gc9a01_d_bc[] = {0x00};
static uint8_t gc9a01_d_ff[] = {0x60, 0x01, 0x04};
static uint8_t gc9a01_d_c3[] = {0x13};
static uint8_t gc9a01_d_c4[] = {0x13};
static uint8_t gc9a01_d_c9[] = {0x22};
static uint8_t gc9a01_d_be[] = {0x11};
static uint8_t gc9a01_d_e1[] = {0x10, 0x0E};
static uint8_t gc9a01_d_df[] = {0x21, 0x0C, 0x02};
static uint8_t gc9a01_d_f0[] = {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A};
static uint8_t gc9a01_d_f1[] = {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F};
static uint8_t gc9a01_d_f2[] = {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A};
static uint8_t gc9a01_d_f3[] = {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F};
static uint8_t gc9a01_d_ed[] = {0x1B, 0x0B};
static uint8_t gc9a01_d_ae[] = {0x77};
static uint8_t gc9a01_d_cd[] = {0x63};
static uint8_t gc9a01_d_70[] = {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03};
static uint8_t gc9a01_d_e8[] = {0x34};
static uint8_t gc9a01_d_62[] = {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70,
                                 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70};
static uint8_t gc9a01_d_63[] = {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70,
                                 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70};
static uint8_t gc9a01_d_64[] = {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07};
static uint8_t gc9a01_d_66[] = {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00};
static uint8_t gc9a01_d_67[] = {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98};
static uint8_t gc9a01_d_74[] = {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00};
static uint8_t gc9a01_d_98[] = {0x3E, 0x07};

static uint16_t gc9a01_swap16(uint16_t value) {
  return (uint16_t)((value << 8) | (value >> 8));
}

static uint16_t gc9a01_map_x(uint16_t x) {
  return x;
}

static uint16_t gc9a01_map_rect_x(uint16_t x, uint16_t width) {
  (void)width;
  return x;
}

static uint16_t gc9a01_map_y(uint16_t y) {
  return y;
}

static uint16_t gc9a01_map_rect_y(uint16_t y, uint16_t height) {
  (void)height;
  return y;
}

static void gc9a01_set_addr_window_locked(uint16_t x, uint16_t y, uint16_t width,
                                          uint16_t height);
static void gc9a01_write_rect_pixels(const uint16_t *pixels, uint16_t stride, uint16_t x, uint16_t y,
                                     uint16_t width, uint16_t height);
static void gc9a01_write_rect_surface(const uint16_t *pixels, uint16_t stride,
                                      const struct display_rect *rect);
static void gc9a01_write_rect_stream_pixels(const uint16_t *pixels, uint16_t stride, uint16_t x,
                                            uint16_t y, uint16_t width, uint16_t height);
static void gc9a01_spi_write_bytes_raw(const uint8_t *data, uint32_t size);
static void gc9a01_spi_write_u8_raw(uint8_t value);
static void gc9a01_spi_wait_idle(void);
static void gc9a01_spi_prepare_tx_transaction(uint32_t bit_length);
static void gc9a01_backlight_init(void);
static void gc9a01_run_init_step(uint32_t index, uint8_t command, const uint8_t *data, uint8_t data_len,
                                 uint16_t delay_ms);

static void gc9a01_signal_settle(void) {
  for (volatile uint32_t i = 0u; i < GC9A01_SIGNAL_SETTLE_CYCLES; ++i) {
    __asm__ volatile("nop");
  }
}

static void gc9a01_begin_write(void) {
  esp32c3_gpio_set_level(GC9A01_PIN_CS, 0);
  gc9a01_signal_settle();
}

static void gc9a01_end_write(void) {
  gc9a01_signal_settle();
  esp32c3_gpio_set_level(GC9A01_PIN_CS, 1);
  gc9a01_signal_settle();
}

static void gc9a01_chip_select(int level) {
  esp32c3_gpio_set_level(GC9A01_PIN_CS, level);
  gc9a01_signal_settle();
}

static void gc9a01_data_command(int level) {
  esp32c3_gpio_set_level(GC9A01_PIN_DC, level);
  gc9a01_signal_settle();
}

static void gc9a01_spi_init(void) {
  uint32_t clock_reg = (3u << SPI_CLKDIV_PRE_SHIFT) | (3u << SPI_CLKCNT_N_SHIFT) |
                       (1u << SPI_CLKCNT_H_SHIFT) | (3u << SPI_CLKCNT_L_SHIFT);

  reg_set_bits(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_SPI2_CLK_EN_BIT);
  reg_set_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_SPI2_RST_BIT);
  reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_SPI2_RST_BIT);

  reg_write(SPI_CLK_GATE_REG, SPI_CLK_EN_BIT | SPI_MST_CLK_ACTIVE_BIT | SPI_MST_CLK_SEL_BIT);
  reg_write(SPI_SLAVE_REG, 0u);
  reg_write(SPI_DMA_CONF_REG, SPI_SLV_TX_SEG_TRANS_CLR_EN_BIT | SPI_SLV_RX_SEG_TRANS_CLR_EN_BIT);
  reg_write(SPI_CTRL_REG, 0u);
  reg_write(SPI_USER_REG, 0u);
  reg_write(SPI_USER1_REG, 0u);
  reg_write(SPI_USER2_REG, 0u);
  reg_write(SPI_CLOCK_REG, clock_reg);
  reg_write(SPI_DOUT_MODE_REG, 0u);
  reg_write(SPI_MISC_REG, 0u);

  esp32c3_gpio_config_output_function(GC9A01_PIN_SCK, PIN_FUNC_ALT_2_VALUE, FSPICLK_OUT_IDX);
  esp32c3_gpio_config_output_function(GC9A01_PIN_MOSI, PIN_FUNC_ALT_2_VALUE, FSPID_OUT_IDX);
  reg_write(SPI_CMD_REG, SPI_UPDATE_BIT);
  gc9a01_spi_wait_idle();
}

static void gc9a01_spi_wait_idle(void) {
  while ((reg_read(SPI_CMD_REG) & (SPI_UPDATE_BIT | SPI_USR_BIT)) != 0u) {
    __asm__ volatile("nop");
  }
}

static void gc9a01_spi_prepare_tx_transaction(uint32_t bit_length) {
  reg_write(SPI_DMA_CONF_REG, SPI_BUF_AFIFO_RST_BIT | SPI_RX_AFIFO_RST_BIT);
  reg_write(SPI_DMA_CONF_REG, SPI_SLV_TX_SEG_TRANS_CLR_EN_BIT | SPI_SLV_RX_SEG_TRANS_CLR_EN_BIT);
  reg_write(SPI_USER_REG, SPI_USR_MOSI_BIT);
  reg_write(SPI_USER1_REG, 0u);
  reg_write(SPI_USER2_REG, 0u);
  reg_clear_bits(SPI_CTRL_REG, SPI_DOUTDIN_BIT);
  reg_clear_bits(SPI_USER_REG, SPI_CK_OUT_EDGE_BIT);
  reg_clear_bits(SPI_MISC_REG, SPI_CK_IDLE_EDGE_BIT);
  reg_write(SPI_MS_DLEN_REG, bit_length - 1u);
  reg_write(SPI_CMD_REG, SPI_UPDATE_BIT);
  gc9a01_spi_wait_idle();
}

static void gc9a01_spi_write_bytes_raw(const uint8_t *data, uint32_t size) {
  if ((data == 0) || (size == 0u)) {
    return;
  }

  for (uint32_t offset = 0u; offset < size;) {
    uint32_t chunk_bytes = size - offset;
    if (chunk_bytes > GC9A01_SPI_MAX_TRANSFER_BYTES) {
      chunk_bytes = GC9A01_SPI_MAX_TRANSFER_BYTES;
    }

    for (uint32_t word_index = 0u; word_index < (GC9A01_SPI_MAX_TRANSFER_BYTES / sizeof(uint32_t));
         ++word_index) {
      uint32_t word = 0u;
      uint32_t byte_index = word_index * sizeof(uint32_t);
      uint32_t copy_len = 0u;

      if (byte_index < chunk_bytes) {
        copy_len = chunk_bytes - byte_index;
        if (copy_len > sizeof(uint32_t)) {
          copy_len = sizeof(uint32_t);
        }
        for (uint32_t i = 0u; i < copy_len; ++i) {
          word |= ((uint32_t)data[offset + byte_index + i]) << (8u * i);
        }
      }

      reg_write(SPI_W0_REG + (word_index * sizeof(uint32_t)), word);
    }

    gc9a01_spi_prepare_tx_transaction(chunk_bytes * 8u);
    reg_write(SPI_CMD_REG, SPI_USR_BIT);
    gc9a01_spi_wait_idle();
    offset += chunk_bytes;
  }
}

static void gc9a01_spi_write_u8_raw(uint8_t value) {
  gc9a01_spi_write_bytes_raw(&value, 1u);
}

static void gc9a01_write_command_locked(uint8_t command) {
  reg_write(RTC_CNTL_STORE5_REG, 0xC320u);
  reg_write(RTC_CNTL_STORE2_REG, command);
  reg_write(RTC_CNTL_STORE3_REG, 0xC320u);
  gc9a01_data_command(0);
  reg_write(RTC_CNTL_STORE3_REG, 0xC321u);
  gc9a01_spi_write_u8_raw(command);
}

static void gc9a01_write_data_bytes_locked(const uint8_t *data, uint32_t size) {
  if ((data == 0) || (size == 0u)) {
    return;
  }

  reg_write(RTC_CNTL_STORE5_REG, 0xC330u);
  gc9a01_data_command(1);
  gc9a01_spi_write_bytes_raw(data, size);
}

static void gc9a01_write_u16_stream_locked(const uint16_t *pixels_be,
                                           uint32_t count) {
  if ((pixels_be == 0) || (count == 0u)) {
    return;
  }

  reg_write(RTC_CNTL_STORE5_REG, 0xC340u);
  gc9a01_data_command(1);
  gc9a01_spi_write_bytes_raw((const uint8_t *)pixels_be, count * sizeof(uint16_t));
}

static void gc9a01_run_init_step(uint32_t index, uint8_t command, const uint8_t *data, uint8_t data_len,
                                 uint16_t delay_ms) {
  reg_write(RTC_CNTL_STORE1_REG, index);
  reg_write(RTC_CNTL_STORE2_REG, command);
  reg_write(RTC_CNTL_STORE0_REG, 0xC180u + (index & 0x3Fu));
  gc9a01_begin_write();
  gc9a01_write_command_locked(command);
  gc9a01_write_data_bytes_locked(data, data_len);
  gc9a01_end_write();
  if (delay_ms != 0u) {
    reg_write(RTC_CNTL_STORE0_REG, 0xC1C0u + (index & 0x3Fu));
    esp32c3_gpio_delay_ms(delay_ms);
  }
}

static void gc9a01_set_rotation0_and_ips_true(void) {
  static const uint8_t madctl = GC9A01_MADCTL_VALUE;

  gc9a01_begin_write();
  gc9a01_write_command_locked(0x36u);
  gc9a01_write_data_bytes_locked(&madctl, 1u);
  gc9a01_write_command_locked(0x21u);
  gc9a01_end_write();
}

static void gc9a01_fill_screen(uint16_t color) {
  uint16_t color_be = gc9a01_swap16(color);

  for (uint16_t index = 0u; index < ESP32C3_PANEL_GC9A01_WIDTH; ++index) {
    g_gc9a01_line_buffer[index] = color_be;
  }

  gc9a01_begin_write();
  gc9a01_set_addr_window_locked(0u, 0u, ESP32C3_PANEL_GC9A01_WIDTH,
                                ESP32C3_PANEL_GC9A01_HEIGHT);
  for (uint16_t row = 0u; row < ESP32C3_PANEL_GC9A01_HEIGHT; ++row) {
    gc9a01_write_u16_stream_locked(g_gc9a01_line_buffer,
                                   ESP32C3_PANEL_GC9A01_WIDTH);
  }
  gc9a01_end_write();
}

void esp32c3_panel_gc9a01_fill_screen(uint16_t color) { gc9a01_fill_screen(color); }

void esp32c3_panel_gc9a01_draw_pixel(uint16_t x, uint16_t y, uint16_t color) {
  uint16_t color_be;

  if ((x >= ESP32C3_PANEL_GC9A01_WIDTH) || (y >= ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return;
  }

  color_be = gc9a01_swap16(color);
  gc9a01_begin_write();
  gc9a01_set_addr_window_locked(gc9a01_map_x(x), gc9a01_map_y(y), 1u, 1u);
  gc9a01_write_u16_stream_locked(&color_be, 1u);
  gc9a01_end_write();
}

void esp32c3_panel_gc9a01_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                    uint16_t color) {
  uint16_t color_be = gc9a01_swap16(color);
  uint16_t mapped_x;
  uint16_t mapped_y;

  if ((width == 0u) || (height == 0u) || ((x + width) > ESP32C3_PANEL_GC9A01_WIDTH) ||
      ((y + height) > ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return;
  }

  mapped_x = gc9a01_map_rect_x(x, width);
  mapped_y = gc9a01_map_rect_y(y, height);

  for (uint16_t index = 0u; index < width; ++index) {
    g_gc9a01_line_buffer[index] = color_be;
  }

  gc9a01_begin_write();
  gc9a01_set_addr_window_locked(mapped_x, mapped_y, width, height);
  for (uint16_t row = 0u; row < height; ++row) {
    gc9a01_write_u16_stream_locked(g_gc9a01_line_buffer, width);
  }
  gc9a01_end_write();
}

static void gc9a01_set_addr_window_locked(uint16_t x, uint16_t y, uint16_t width,
                                          uint16_t height) {
  uint8_t caset[4] = {(uint8_t)(x >> 8), (uint8_t)(x & 0xFFu),
                      (uint8_t)((x + width - 1u) >> 8),
                      (uint8_t)((x + width - 1u) & 0xFFu)};
  uint8_t raset[4] = {(uint8_t)(y >> 8), (uint8_t)(y & 0xFFu),
                      (uint8_t)((y + height - 1u) >> 8), (uint8_t)((y + height - 1u) & 0xFFu)};

  gc9a01_write_command_locked(0x2Au);
  gc9a01_write_data_bytes_locked(caset, sizeof(caset));
  gc9a01_end_write();
  gc9a01_begin_write();
  gc9a01_write_command_locked(0x2Bu);
  gc9a01_write_data_bytes_locked(raset, sizeof(raset));
  gc9a01_end_write();
  gc9a01_begin_write();
  gc9a01_write_command_locked(0x2Cu);
#if GC9A01_RAMWR_TO_DATA_CYCLES > 0u
  for (volatile uint32_t i = 0u; i < GC9A01_RAMWR_TO_DATA_CYCLES; ++i) {
    __asm__ volatile("nop");
  }
#endif
}

void esp32c3_panel_gc9a01_begin_batch(void) {
  (void)0;
}

void esp32c3_panel_gc9a01_end_batch(void) {
  (void)0;
}

static void gc9a01_write_rect_pixels(const uint16_t *pixels, uint16_t stride, uint16_t x, uint16_t y,
                                     uint16_t width, uint16_t height) {
  if ((pixels == 0) || (stride < width) || (width == 0u) || (height == 0u)) {
    return;
  }

  gc9a01_write_rect_stream_pixels(pixels, stride, x, y, width, height);
}

static void gc9a01_write_rect_surface(const uint16_t *pixels, uint16_t stride,
                                      const struct display_rect *rect) {
  if ((pixels == 0) || (rect == 0) || (stride == 0u)) {
    return;
  }

  gc9a01_write_rect_stream_pixels(&pixels[(uint32_t)rect->y * stride + rect->x], stride, rect->x,
                                  rect->y, rect->width, rect->height);
}

static void gc9a01_write_rect_stream_pixels(const uint16_t *pixels, uint16_t stride, uint16_t x,
                                            uint16_t y, uint16_t width, uint16_t height) {
  uint16_t mapped_x;
  uint16_t mapped_y;

  if ((pixels == 0) || (stride < width) || (width == 0u) || (height == 0u) ||
      ((x + width) > ESP32C3_PANEL_GC9A01_WIDTH) ||
      ((y + height) > ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return;
  }

  mapped_x = gc9a01_map_rect_x(x, width);
  mapped_y = gc9a01_map_rect_y(y, height);

  gc9a01_begin_write();
  gc9a01_set_addr_window_locked(mapped_x, mapped_y, width, height);
  for (uint16_t row = 0u; row < height; ++row) {
    const uint16_t *source_row = &pixels[(uint32_t)row * stride];

    for (uint16_t col = 0u; col < width; ++col) {
      g_gc9a01_line_buffer[col] = gc9a01_swap16(source_row[col]);
    }

    gc9a01_write_u16_stream_locked(g_gc9a01_line_buffer, width);
  }
  gc9a01_end_write();
}

void esp32c3_panel_gc9a01_backlight_set(uint8_t percent) {
  uint32_t duty = percent == 0u ? 0u : GC9A01_BL_LEDC_DUTY;

  reg_write(LEDC_LSCH0_DUTY_REG, duty << 4u);
  reg_write(LEDC_LSCH0_CONF1_REG,
            LEDC_DUTY_START_LSCH0 | LEDC_DUTY_INC_LSCH0 | (1u << LEDC_DUTY_NUM_LSCH0_S) |
                (1u << LEDC_DUTY_CYCLE_LSCH0_S) | (0u << LEDC_DUTY_SCALE_LSCH0_S));
  reg_write(LEDC_LSCH0_CONF0_REG,
            reg_read(LEDC_LSCH0_CONF0_REG) | LEDC_SIG_OUT_EN_LSCH0 | LEDC_PARA_UP_LSCH0);
}

static void gc9a01_backlight_init(void) {
  reg_set_bits(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_LEDC_CLK_EN_BIT);
  reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_LEDC_RST_BIT);

  reg_write(LEDC_CONF_REG, LEDC_CLK_EN | (1u << LEDC_APB_CLK_SEL_S));
  reg_write(LEDC_LSTIMER0_CONF_REG,
            (GC9A01_BL_LEDC_DIV << LEDC_CLK_DIV_LSTIMER0_S) |
                (GC9A01_BL_LEDC_DUTY_RES << LEDC_LSTIMER0_DUTY_RES_S) | LEDC_LSTIMER0_PARA_UP);
  reg_write(LEDC_LSCH0_HPOINT_REG, 0u);
  reg_write(LEDC_LSCH0_DUTY_REG, 0u);
  reg_write(LEDC_LSCH0_CONF0_REG, (0u << LEDC_TIMER_SEL_LSCH0_S) | LEDC_SIG_OUT_EN_LSCH0 |
                                      LEDC_PARA_UP_LSCH0);
  reg_write(LEDC_LSCH0_CONF1_REG,
            LEDC_DUTY_START_LSCH0 | LEDC_DUTY_INC_LSCH0 | (1u << LEDC_DUTY_NUM_LSCH0_S) |
                (1u << LEDC_DUTY_CYCLE_LSCH0_S) | (0u << LEDC_DUTY_SCALE_LSCH0_S));
}

void esp32c3_panel_gc9a01_init(void) {
  reg_write(RTC_CNTL_STORE0_REG, 0xC110u);
  esp32c3_gpio_config_output(GC9A01_PIN_RST);
  esp32c3_gpio_config_output(GC9A01_PIN_DC);
  esp32c3_gpio_config_output(GC9A01_PIN_BL);
  esp32c3_gpio_config_output(GC9A01_PIN_CS);
  esp32c3_gpio_set_level(GC9A01_PIN_BL, 0);

  gc9a01_chip_select(1);
  gc9a01_data_command(1);
  gc9a01_spi_init();
  reg_write(RTC_CNTL_STORE0_REG, 0xC111u);
  gc9a01_backlight_init();
  esp32c3_panel_gc9a01_backlight_set(0u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC112u);

  reg_write(RTC_CNTL_STORE3_REG, 0xC310u);
  esp32c3_gpio_set_level(GC9A01_PIN_RST, 1);
  reg_write(RTC_CNTL_STORE3_REG, 0xC311u);
  esp32c3_gpio_delay_ms(200u);
  reg_write(RTC_CNTL_STORE3_REG, 0xC312u);
  esp32c3_gpio_set_level(GC9A01_PIN_RST, 0);
  reg_write(RTC_CNTL_STORE3_REG, 0xC313u);
  esp32c3_gpio_delay_ms(200u);
  reg_write(RTC_CNTL_STORE3_REG, 0xC314u);
  esp32c3_gpio_set_level(GC9A01_PIN_RST, 1);
  reg_write(RTC_CNTL_STORE3_REG, 0xC315u);
  esp32c3_gpio_delay_ms(200u);
  reg_write(RTC_CNTL_STORE3_REG, 0xC316u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC113u);

  gc9a01_run_init_step(0u, 0xEFu, 0, 0u, 0u);
  gc9a01_run_init_step(1u, 0xEBu, gc9a01_d_eb_14, 1u, 0u);
  gc9a01_run_init_step(2u, 0xFEu, 0, 0u, 0u);
  gc9a01_run_init_step(3u, 0xEFu, 0, 0u, 0u);
  gc9a01_run_init_step(4u, 0xEBu, gc9a01_d_eb_14, 1u, 0u);
  gc9a01_run_init_step(5u, 0x84u, gc9a01_d_84, 1u, 0u);
  gc9a01_run_init_step(6u, 0x85u, gc9a01_d_85, 1u, 0u);
  gc9a01_run_init_step(7u, 0x86u, gc9a01_d_86, 1u, 0u);
  gc9a01_run_init_step(8u, 0x87u, gc9a01_d_87, 1u, 0u);
  gc9a01_run_init_step(9u, 0x88u, gc9a01_d_88, 1u, 0u);
  gc9a01_run_init_step(10u, 0x89u, gc9a01_d_89, 1u, 0u);
  gc9a01_run_init_step(11u, 0x8Au, gc9a01_d_8a, 1u, 0u);
  gc9a01_run_init_step(12u, 0x8Bu, gc9a01_d_8b, 1u, 0u);
  gc9a01_run_init_step(13u, 0x8Cu, gc9a01_d_8c, 1u, 0u);
  gc9a01_run_init_step(14u, 0x8Du, gc9a01_d_8d, 1u, 0u);
  gc9a01_run_init_step(15u, 0x8Eu, gc9a01_d_8e, 1u, 0u);
  gc9a01_run_init_step(16u, 0x8Fu, gc9a01_d_8f, 1u, 0u);
  gc9a01_run_init_step(17u, 0xB6u, gc9a01_d_b6, 2u, 0u);
  gc9a01_run_init_step(18u, 0x3Au, gc9a01_d_3a, 1u, 0u);
  gc9a01_run_init_step(19u, 0x90u, gc9a01_d_90, 4u, 0u);
  gc9a01_run_init_step(20u, 0xBDu, gc9a01_d_bd, 1u, 0u);
  gc9a01_run_init_step(21u, 0xBCu, gc9a01_d_bc, 1u, 0u);
  gc9a01_run_init_step(22u, 0xFFu, gc9a01_d_ff, 3u, 0u);
  gc9a01_run_init_step(23u, 0xC3u, gc9a01_d_c3, 1u, 0u);
  gc9a01_run_init_step(24u, 0xC4u, gc9a01_d_c4, 1u, 0u);
  gc9a01_run_init_step(25u, 0xC9u, gc9a01_d_c9, 1u, 0u);
  gc9a01_run_init_step(26u, 0xBEu, gc9a01_d_be, 1u, 0u);
  gc9a01_run_init_step(27u, 0xE1u, gc9a01_d_e1, 2u, 0u);
  gc9a01_run_init_step(28u, 0xDFu, gc9a01_d_df, 3u, 0u);
  gc9a01_run_init_step(29u, 0xF0u, gc9a01_d_f0, 6u, 0u);
  gc9a01_run_init_step(30u, 0xF1u, gc9a01_d_f1, 6u, 0u);
  gc9a01_run_init_step(31u, 0xF2u, gc9a01_d_f2, 6u, 0u);
  gc9a01_run_init_step(32u, 0xF3u, gc9a01_d_f3, 6u, 0u);
  gc9a01_run_init_step(33u, 0xEDu, gc9a01_d_ed, 2u, 0u);
  gc9a01_run_init_step(34u, 0xAEu, gc9a01_d_ae, 1u, 0u);
  gc9a01_run_init_step(35u, 0xCDu, gc9a01_d_cd, 1u, 0u);
  gc9a01_run_init_step(36u, 0x70u, gc9a01_d_70, 9u, 0u);
  gc9a01_run_init_step(37u, 0xE8u, gc9a01_d_e8, 1u, 0u);
  gc9a01_run_init_step(38u, 0x62u, gc9a01_d_62, 12u, 0u);
  gc9a01_run_init_step(39u, 0x63u, gc9a01_d_63, 12u, 0u);
  gc9a01_run_init_step(40u, 0x64u, gc9a01_d_64, 7u, 0u);
  gc9a01_run_init_step(41u, 0x66u, gc9a01_d_66, 10u, 0u);
  gc9a01_run_init_step(42u, 0x67u, gc9a01_d_67, 10u, 0u);
  gc9a01_run_init_step(43u, 0x74u, gc9a01_d_74, 7u, 0u);
  gc9a01_run_init_step(44u, 0x98u, gc9a01_d_98, 2u, 0u);
  gc9a01_run_init_step(45u, 0x35u, 0, 0u, 0u);
  gc9a01_run_init_step(46u, 0x11u, 0, 0u, 120u);
  gc9a01_run_init_step(47u, 0x29u, 0, 0u, 20u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC114u);

  gc9a01_set_rotation0_and_ips_true();
  reg_write(RTC_CNTL_STORE0_REG, 0xC115u);

  esp32c3_gpio_delay_ms(120u);
  gc9a01_fill_screen(0xFFFFu);
  reg_write(RTC_CNTL_STORE0_REG, 0xC116u);
  esp32c3_gpio_delay_ms(20u);
  gc9a01_fill_screen(0xFFFFu);
  reg_write(RTC_CNTL_STORE0_REG, 0xC117u);
  esp32c3_gpio_config_output_function(GC9A01_PIN_BL, PIN_FUNC_GPIO_VALUE, LEDC_LS_SIG_OUT0_IDX);
  esp32c3_panel_gc9a01_backlight_set(100u);
  reg_write(RTC_CNTL_STORE0_REG, 0xC118u);
}

int32_t esp32c3_panel_gc9a01_flush_rect(const uint16_t *pixels, uint16_t stride,
                                        const struct display_rect *rect) {
  if ((pixels == 0) || (rect == 0) || (stride == 0u) || (rect->width == 0u) || (rect->height == 0u) ||
      ((rect->x + rect->width) > ESP32C3_PANEL_GC9A01_WIDTH) ||
      ((rect->y + rect->height) > ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  gc9a01_write_rect_surface(pixels, stride, rect);
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t esp32c3_panel_gc9a01_flush_rect_pixels(const uint16_t *pixels, uint16_t stride,
                                               const struct display_rect *rect) {
  if ((pixels == 0) || (rect == 0) || (stride == 0u) || (rect->width == 0u) || (rect->height == 0u) ||
      (stride < rect->width) || ((rect->x + rect->width) > ESP32C3_PANEL_GC9A01_WIDTH) ||
      ((rect->y + rect->height) > ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  gc9a01_write_rect_pixels(pixels, stride, rect->x, rect->y, rect->width, rect->height);
  return RUNTIME_SYSCALL_STATUS_OK;
}

int32_t esp32c3_panel_gc9a01_flush_rect_stream(const uint16_t *pixels, uint16_t stride,
                                               const struct display_rect *rect) {
  if ((pixels == 0) || (rect == 0) || (stride == 0u) || (rect->width == 0u) || (rect->height == 0u) ||
      ((rect->x + rect->width) > ESP32C3_PANEL_GC9A01_WIDTH) ||
      ((rect->y + rect->height) > ESP32C3_PANEL_GC9A01_HEIGHT)) {
    return RUNTIME_SYSCALL_STATUS_EINVAL;
  }

  gc9a01_write_rect_surface(pixels, stride, rect);
  return RUNTIME_SYSCALL_STATUS_OK;
}
