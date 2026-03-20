#include "runtime.h"
#include "esp32c3_gpio.h"
#include "soc.h"

#define CPU_GPIO_INTR_NUM 3u
#define CPU_RTC_CORE_INTR_NUM 2u
#define CPU_SYSTIMER_INTR_NUM 7u
#define CPU_SOFTWARE_TEST_INTR_NUM 30u

static uint8_t g_timer_interrupt_seen;
static uint8_t g_preempt_redirect_seen;
uint32_t g_trap_preempt_resume_pc;

extern void preempt_return_trampoline(void);

static inline void csr_write_mtvec(uint32_t value) {
  __asm__ volatile("csrw mtvec, %0" ::"r"(value));
}

static inline void csr_write_mepc(uint32_t value) {
  __asm__ volatile("csrw mepc, %0" ::"r"(value));
}

static inline uint32_t csr_read_mtvec(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mtvec" : "=r"(value));
  return value;
}

static inline uint32_t csr_read_mepc(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mepc" : "=r"(value));
  return value;
}

void trap_init(void) {
  uintptr_t mtvec_base = (uintptr_t)__mtvec_base;
  csr_write_mtvec((uint32_t)(mtvec_base | 1u));
}

uint32_t trap_timer_interrupt_seen(void) {
  return (uint32_t)g_timer_interrupt_seen;
}

uint32_t trap_mtvec_read(void) {
  return csr_read_mtvec();
}

void trap_interrupt_c(uint32_t mcause) {
  if ((mcause & ~MCAUSE_INTERRUPT_BIT) == CPU_SOFTWARE_TEST_INTR_NUM) {
    reg_write(RTC_CNTL_STORE0_REG, 0xE11Cu);
    reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, 0u);
    interrupts_ack(CPU_SOFTWARE_TEST_INTR_NUM);
    console_log("INTR", "software interrupt");
    return;
  }

  if ((mcause & ~MCAUSE_INTERRUPT_BIT) == CPU_RTC_CORE_INTR_NUM) {
    reg_write(RTC_CNTL_STORE0_REG, 0xE102u);
    reg_write(RTC_CNTL_STORE4_REG, reg_read(RTC_CNTL_INT_RAW_REG));
    reg_write(RTC_CNTL_STORE5_REG, reg_read(RTC_CNTL_INT_ST_REG));
    reg_write(RTC_CNTL_STORE6_REG, reg_read(RTC_CNTL_INT_ENA_REG));
    reg_write(RTC_CNTL_STORE7_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
    reg_write(RTC_CNTL_INT_CLR_REG, 0xFFFFFFFFu);
    interrupts_ack(CPU_RTC_CORE_INTR_NUM);
    return;
  }

  if ((mcause & ~MCAUSE_INTERRUPT_BIT) == CPU_SYSTIMER_INTR_NUM) {
    if ((reg_read(SYSTEM_CPU_INTR_FROM_CPU_0_REG) & SYSTEM_CPU_INTR_FROM_CPU_0_BIT) != 0u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xE117u);
      reg_write(RTC_CNTL_STORE4_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
      reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTEM_CPU_INTR_FROM_CPU_0_REG));
      reg_write(RTC_CNTL_STORE7_REG, reg_read(SYSTIMER_INT_ST_REG));
      reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, 0u);
      interrupts_ack(CPU_SYSTIMER_INTR_NUM);
      return;
    }

    if ((reg_read(SYSTIMER_INT_RAW_REG) & SYSTIMER_TARGET0_INT_ST_BIT) != 0u ||
        (reg_read(SYSTIMER_INT_ST_REG) & SYSTIMER_TARGET0_INT_ST_BIT) != 0u) {
      if (g_timer_interrupt_seen == 0u) {
        g_timer_interrupt_seen = 1u;
        reg_write(RTC_CNTL_STORE0_REG, 0xE107u);
        reg_write(RTC_CNTL_STORE4_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
        reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTIMER_INT_RAW_REG));
        reg_write(RTC_CNTL_STORE7_REG, reg_read(SYSTIMER_INT_ST_REG));
      }
      systimer_tick_isr();
      if (scheduler_preempt_supported() && scheduler_preempt_pending() &&
          (preempt_disable_depth() == 0u) && (process_current_pid() != 0u)) {
        if (g_preempt_redirect_seen == 0u) {
          g_preempt_redirect_seen = 1u;
          reg_write(RTC_CNTL_STORE5_REG, 0xE108u);
          reg_write(RTC_CNTL_STORE6_REG, csr_read_mepc());
          reg_write(RTC_CNTL_STORE7_REG, scheduler_switch_count_read());
        }
        g_trap_preempt_resume_pc = csr_read_mepc();
        scheduler_preempt_clear_pending();
        csr_write_mepc((uint32_t)(uintptr_t)preempt_return_trampoline);
      }
      interrupts_ack(CPU_SYSTIMER_INTR_NUM);
      return;
    }

    reg_write(RTC_CNTL_STORE0_REG, 0xE110u);
    reg_write(RTC_CNTL_STORE4_REG, reg_read(SYSTIMER_INT_RAW_REG));
    reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTIMER_INT_ST_REG));
    reg_write(RTC_CNTL_STORE7_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
    interrupts_ack(CPU_SYSTIMER_INTR_NUM);
    return;
  }

  if ((mcause & ~MCAUSE_INTERRUPT_BIT) == CPU_GPIO_INTR_NUM) {
    uint32_t pending = reg_read(GPIO_STATUS_REG) & 0x003FFFFFu;
    if (pending != 0u) {
      reg_write(RTC_CNTL_STORE0_REG, 0xE130u);
      reg_write(RTC_CNTL_STORE4_REG, pending);
      reg_write(GPIO_STATUS_W1TC_REG, pending);
      esp32c3_gpio_interrupt_dispatch(pending);
    }
    interrupts_ack(CPU_GPIO_INTR_NUM);
    return;
  }

  reg_write(RTC_CNTL_STORE0_REG, 0xE100u | (mcause & 0xFFu));
  reg_write(RTC_CNTL_STORE5_REG, DIAG_TIMER_PATH_BAD);
  reg_write(RTC_CNTL_STORE6_REG, DIAGD_UNEXPECTED_INTERRUPT);
  reg_write(RTC_CNTL_STORE7_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
  console_log_hex32("TRAP", "unexpected interrupt mcause=", mcause);
  panic_halt();
}

void trap_exception_c(uint32_t mcause, uint32_t mepc, uint32_t mtval) {
  reg_write(RTC_CNTL_STORE0_REG, 0xE200u | (mcause & 0xFFu));
  reg_write(RTC_CNTL_STORE5_REG, DIAG_TRAP_EXCEPTION);
  reg_write(RTC_CNTL_STORE6_REG, DIAGD_EXCEPTION_MCAUSE | (mcause & 0xFFu));
  reg_write(RTC_CNTL_STORE7_REG, mepc);
  reg_write(RTC_CNTL_STORE2_REG, mepc);
  reg_write(RTC_CNTL_STORE3_REG, mtval);
  console_log_hex32("TRAP", "exception mcause=", mcause);
  console_log_hex32("TRAP", "exception mepc=", mepc);
  console_log_hex32("TRAP", "exception mtval=", mtval);

  /* Advance past the faulting instruction to avoid re-entering forever if resumed. */
  csr_write_mepc(mepc + 4u);
  panic_halt();
}
