#include "runtime.h"
#include "soc.h"

static char g_boot_dbg_tag[] = "BOOT";
static char g_boot_dbg_console[] = "console_online_dram";
static char g_boot_dbg_kernel[] = "jump_kernel_dram";

static inline void fence_rw_rw(void) {
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

static inline uint32_t csr_read_mtvec(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mtvec" : "=r"(value));
  return value;
}

static void disable_rtc_wdt(void) {
  reg_write(RTC_CNTL_WDTWPROTECT_REG, 0x50D83AA1u);
  reg_clear_bits(RTC_CNTL_WDTCONFIG0_REG, RTC_CNTL_WDT_FLASHBOOT_MOD_EN_BIT | RTC_CNTL_WDT_EN_BIT);
  reg_write(RTC_CNTL_WDTWPROTECT_REG, 0u);
}

static void disable_timg_wdt(uint32_t timer_group_base) {
  reg_write(TIMG_WDTWPROTECT_REG(timer_group_base), TIMG_WDT_WKEY_VALUE);
  reg_write(TIMG_WDTCONFIG0_REG(timer_group_base), TIMG_WDT_DISABLE_VALUE);
  reg_write(TIMG_WDTWPROTECT_REG(timer_group_base), 0u);
}

static void boot_trace(uint32_t marker) {
  reg_write(RTC_CNTL_STORE0_REG, marker);
}

static void __attribute__((unused)) boot_dump_previous_trace(void) {
  uint32_t reset_state = reg_read(RTC_CNTL_RESET_STATE_REG);
  uint32_t diag_reason = reg_read(RTC_CNTL_STORE5_REG);
  console_log_hex32("BOOT", "prev store0=", reg_read(RTC_CNTL_STORE0_REG));
  console_log_hex32("BOOT", "prev store1=", reg_read(RTC_CNTL_STORE1_REG));
  console_log_hex32("BOOT", "prev store2=", reg_read(RTC_CNTL_STORE2_REG));
  console_log_hex32("BOOT", "prev store3=", reg_read(RTC_CNTL_STORE3_REG));
  console_log_hex32("BOOT", "prev store4=", reg_read(RTC_CNTL_STORE4_REG));
  console_log_hex32("BOOT", "prev store5=", diag_reason);
  console_log_hex32("BOOT", "prev store6=", reg_read(RTC_CNTL_STORE6_REG));
  console_log_hex32("BOOT", "prev store7=", reg_read(RTC_CNTL_STORE7_REG));
  switch (diag_reason) {
    case DIAG_NONE:
      console_log("BOOT", "diag=none");
      break;
    case DIAG_PRE_TIMER_STALL:
      console_log("BOOT", "diag=pre_timer_stall");
      break;
    case DIAG_TIMER_PATH_BAD:
      console_log("BOOT", "diag=timer_path_bad");
      break;
    case DIAG_POST_TIMER_STALL:
      console_log("BOOT", "diag=post_timer_stall");
      break;
    case DIAG_TRAP_EXCEPTION:
      console_log("BOOT", "diag=trap_exception");
      break;
    case DIAG_SCHED_OR_STACK:
      console_log("BOOT", "diag=sched_or_stack");
      break;
    default:
      console_log("BOOT", "diag=unknown");
      break;
  }
  console_log_hex32("BOOT", "reset_state=", reset_state);
  console_log_u32("BOOT", "reset_cause_procpu=", reset_state & RTC_CNTL_RESET_CAUSE_PROCPU_MASK);
}

static void boot_clear_trace(void) {
  reg_write(RTC_CNTL_STORE0_REG, 0u);
  reg_write(RTC_CNTL_STORE1_REG, 0u);
  reg_write(RTC_CNTL_STORE2_REG, 0u);
  reg_write(RTC_CNTL_STORE3_REG, 0u);
  reg_write(RTC_CNTL_STORE4_REG, 0u);
  reg_write(RTC_CNTL_STORE5_REG, 0u);
  reg_write(RTC_CNTL_STORE6_REG, 0u);
  reg_write(RTC_CNTL_STORE7_REG, 0u);
}

void panic_halt(void) {
  while (1) {
    __asm__ volatile("wfi");
  }
}

void boot_main(void) {
  console_init();
  boot_clear_trace();
  boot_trace(0xB001u);
  console_log(g_boot_dbg_tag, g_boot_dbg_console);
  disable_rtc_wdt();
  disable_timg_wdt(TIMG0_BASE);
  disable_timg_wdt(TIMG1_BASE);
  boot_trace(0xB002u);
  boot_trace(0xB003u);
  trap_init();
  boot_trace(0xB004u);
  systimer_unit0_init();
  boot_trace(0xB005u);

  fence_rw_rw();
  console_log(g_boot_dbg_tag, g_boot_dbg_kernel);

  boot_trace(0xB006u);
  kernel_main();
  boot_trace(0xB0FFu);
  panic_halt();
}
