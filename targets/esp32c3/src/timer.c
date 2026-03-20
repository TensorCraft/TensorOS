#include "runtime.h"
#include "soc.h"

#define SYSTIMER_TICK_PERIOD_MS    1u
#define SYSTIMER_TICKS_PER_US      16u
#define SYSTIMER_TICK_PERIOD_TICKS (1000u * SYSTIMER_TICKS_PER_US)

static volatile uint64_t g_kernel_ticks;
static uint32_t g_kernel_tick_log_counter;
static uint32_t g_last_tick_switch_count;
static uint32_t g_stalled_tick_windows;
static uint32_t g_long_run_milestone_counter;
static uint32_t g_long_run_tick_window;

static inline uint32_t interrupts_save_disable(void) {
  uint32_t mstatus;
  __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));
  return mstatus;
}

static inline void interrupts_restore(uint32_t mstatus) {
  if ((mstatus & BIT(3)) != 0u) {
    __asm__ volatile("csrsi mstatus, 8");
  }
}

void systimer_tick_init(void) {
  uint32_t conf = reg_read(SYSTIMER_TARGET0_CONF_REG);
  uint32_t period_conf;
  uint32_t oneshot_conf;

  reg_set_bits(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_SYSTIMER_CLK_EN_BIT);
  reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_SYSTIMER_RST_BIT);
  reg_set_bits(SYSTIMER_CONF_REG, SYSTIMER_CLK_EN_BIT | SYSTIMER_TIMER_UNIT0_WORK_EN_BIT);
  reg_clear_bits(SYSTIMER_CONF_REG, SYSTIMER_TIMER_UNIT0_CORE0_STALL_EN_BIT);
  reg_write(SYSTIMER_UNIT0_LOAD_HI_REG, 0u);
  reg_write(SYSTIMER_UNIT0_LOAD_LO_REG, 0u);
  reg_write(SYSTIMER_UNIT0_LOAD_REG, SYSTIMER_TIMER_UNIT0_LOAD_BIT);
  reg_write(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_CLR_BIT);
  reg_clear_bits(SYSTIMER_INT_ENA_REG, SYSTIMER_TARGET0_INT_ENA_BIT);
  reg_clear_bits(SYSTIMER_CONF_REG, SYSTIMER_TARGET0_WORK_EN_BIT);

  conf &= ~(SYSTIMER_TARGET0_PERIOD_MASK | SYSTIMER_TARGET0_PERIOD_MODE_BIT |
            SYSTIMER_TARGET0_TIMER_UNIT_SEL_BIT);
  oneshot_conf = conf;
  period_conf = conf | SYSTIMER_TICK_PERIOD_TICKS | SYSTIMER_TARGET0_PERIOD_MODE_BIT;

  reg_write(SYSTIMER_TARGET0_CONF_REG, oneshot_conf);
  reg_write(SYSTIMER_TARGET0_CONF_REG, oneshot_conf | SYSTIMER_TICK_PERIOD_TICKS);
  reg_write(SYSTIMER_COMP0_LOAD_REG, SYSTIMER_TIMER_COMP0_LOAD_BIT);
  reg_set_bits(SYSTIMER_CONF_REG, SYSTIMER_TARGET0_WORK_EN_BIT);
  reg_write(SYSTIMER_TARGET0_CONF_REG, period_conf);
  reg_write(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_CLR_BIT);
  reg_set_bits(SYSTIMER_INT_ENA_REG, SYSTIMER_TARGET0_INT_ENA_BIT);
  reg_write(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_CLR_BIT);

  reg_write(RTC_CNTL_STORE1_REG, reg_read(SYSTIMER_CONF_REG));
  reg_write(RTC_CNTL_STORE2_REG, reg_read(SYSTIMER_TARGET0_CONF_REG));
  reg_write(RTC_CNTL_STORE4_REG, reg_read(SYSTIMER_UNIT0_VALUE_LO_REG));
  reg_write(RTC_CNTL_STORE5_REG, reg_read(SYSTIMER_INT_ENA_REG));
  reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTIMER_INT_RAW_REG));
  reg_write(RTC_CNTL_STORE7_REG, reg_read(SYSTIMER_INT_ST_REG));
}

void systimer_tick_isr(void) {
  uint32_t int_st = reg_read(SYSTIMER_INT_ST_REG);
  if ((int_st & SYSTIMER_TARGET0_INT_ST_BIT) != 0u) {
    uint32_t switch_count = scheduler_switch_count_read();
    reg_write(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_CLR_BIT);

    ++g_kernel_ticks;
    ++g_kernel_tick_log_counter;
    ++g_long_run_tick_window;
    process_tick(g_kernel_ticks);

    if (g_kernel_ticks == 1u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xC001u);
      reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTIMER_INT_RAW_REG));
      reg_write(RTC_CNTL_STORE7_REG, int_st);
    }

    if (switch_count == g_last_tick_switch_count) {
      ++g_stalled_tick_windows;
      if (g_stalled_tick_windows >= 4u) {
        reg_write(RTC_CNTL_STORE5_REG, DIAG_POST_TIMER_STALL);
        reg_write(RTC_CNTL_STORE6_REG, DIAGD_TICK_SEEN_BUT_SWITCH_STOPPED);
        reg_write(RTC_CNTL_STORE7_REG, switch_count);
      }
    } else {
      g_last_tick_switch_count = switch_count;
      g_stalled_tick_windows = 0u;
    }

    if (g_kernel_tick_log_counter == 1000u) {
      g_kernel_tick_log_counter = 0u;
      reg_write(RTC_CNTL_STORE0_REG, 0xC3E8u);
    }

    if (g_long_run_tick_window == 5000u) {
      g_long_run_tick_window = 0u;
      ++g_long_run_milestone_counter;
      reg_write(RTC_CNTL_STORE1_REG, g_long_run_milestone_counter);
      reg_write(RTC_CNTL_STORE2_REG, switch_count);
      reg_write(RTC_CNTL_STORE3_REG, (uint32_t)g_kernel_ticks);
    }
  } else {
    uint32_t int_raw = reg_read(SYSTIMER_INT_RAW_REG);
    reg_write(RTC_CNTL_STORE5_REG, DIAG_TIMER_PATH_BAD);
    reg_write(RTC_CNTL_STORE6_REG,
              (int_raw & SYSTIMER_TARGET0_INT_ST_BIT) != 0u ? DIAGD_TIMER_RAW1_ST0 : DIAGD_TIMER_VECTOR_BUT_ST0);
    reg_write(RTC_CNTL_STORE7_REG, int_st);
  }
}

uint64_t kernel_ticks_read(void) {
  uint32_t mstatus = interrupts_save_disable();
  uint64_t ticks = g_kernel_ticks;
  interrupts_restore(mstatus);
  return ticks;
}

void systimer_tick_debug_dump(void) {
  uint64_t now = systimer_unit0_read();
  console_log_hex32("TIMER", "conf=", reg_read(SYSTIMER_CONF_REG));
  console_log_hex32("TIMER", "target0_conf=", reg_read(SYSTIMER_TARGET0_CONF_REG));
  console_log_hex32("TIMER", "target0_hi=", reg_read(SYSTIMER_TARGET0_HI_REG));
  console_log_hex32("TIMER", "target0_lo=", reg_read(SYSTIMER_TARGET0_LO_REG));
  console_log_hex32("TIMER", "int_ena=", reg_read(SYSTIMER_INT_ENA_REG));
  console_log_hex32("TIMER", "int_raw=", reg_read(SYSTIMER_INT_RAW_REG));
  console_log_hex32("TIMER", "int_st=", reg_read(SYSTIMER_INT_ST_REG));
  console_log_hex32("TIMER", "now_hi=", (uint32_t)(now >> 32));
  console_log_hex32("TIMER", "now_lo=", (uint32_t)now);
}
