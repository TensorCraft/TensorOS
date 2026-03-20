#include "runtime.h"
#include "esp32c3_gpio.h"
#include "soc.h"

#define CPU_GPIO_INTR_NUM 3u
#define CPU_SYSTIMER_INTR_NUM 7u
#define CPU_RTC_CORE_INTR_NUM 2u
#define CPU_INVALID_INTR_NUM 6u
#define CPU_SOFTWARE_TEST_INTR_NUM 30u
#define PERIPHERAL_INTERRUPT_SOURCE_COUNT 62u
#define INTR_TYPE_LEVEL 0u
#define INTR_TYPE_EDGE 1u
#define ROM_ESPRV_INTC_INT_SET_PRIORITY_ADDR 0x400005E0u
#define ROM_ESPRV_INTC_INT_SET_THRESHOLD_ADDR 0x400005E4u
#define ROM_ESPRV_INTC_INT_ENABLE_ADDR 0x400005E8u
#define ROM_ESPRV_INTC_INT_DISABLE_ADDR 0x400005ECu
#define ROM_ESPRV_INTC_INT_SET_TYPE_ADDR 0x400005F0u
#define ROM_INTR_MATRIX_SET_ADDR 0x400005F4u

struct irq_route_config {
  uint32_t source;
  uint32_t cpu_interrupt_number;
  uint32_t priority;
  uint32_t type;
};

static struct irq_route_config g_systimer_irq = {
    .source = ETS_SYSTIMER_TARGET0_INTR_SOURCE,
    .cpu_interrupt_number = CPU_SYSTIMER_INTR_NUM,
    .priority = 1u,
    .type = INTR_TYPE_LEVEL,
};

static inline void csr_set_bits_mstatus(uint32_t value) {
  __asm__ volatile("csrs mstatus, %0" ::"r"(value));
}

static inline uint32_t csr_read_mstatus(void) {
  uint32_t value;
  __asm__ volatile("csrr %0, mstatus" : "=r"(value));
  return value;
}

typedef void (*rom_intr_matrix_set_t)(int cpu_core, uint32_t source, uint32_t cpu_interrupt_number);

static inline rom_intr_matrix_set_t rom_intr_matrix_set_fn(void) {
  return (rom_intr_matrix_set_t)ROM_INTR_MATRIX_SET_ADDR;
}

static inline void interrupt_matrix_route(uint32_t source, uint32_t cpu_interrupt_number) {
  rom_intr_matrix_set_fn()(0, source, cpu_interrupt_number);
}

static inline void interrupt_set_priority(uint32_t cpu_interrupt_number, uint32_t priority) {
  reg_write(INTERRUPT_CPU_INT_PRIO_REG(cpu_interrupt_number), priority & 0x7u);
}

static inline void interrupt_set_type(uint32_t cpu_interrupt_number, uint32_t type) {
  if (type == INTR_TYPE_EDGE) {
    reg_set_bits(INTERRUPT_CORE0_CPU_INT_TYPE_REG, BIT(cpu_interrupt_number));
  } else {
    reg_clear_bits(INTERRUPT_CORE0_CPU_INT_TYPE_REG, BIT(cpu_interrupt_number));
  }
}

static inline void interrupt_enable_mask(uint32_t mask) {
  reg_set_bits(INTERRUPT_CORE0_CPU_INT_ENABLE_REG, mask);
}

static inline void interrupt_disable_mask(uint32_t mask) {
  reg_clear_bits(INTERRUPT_CORE0_CPU_INT_ENABLE_REG, mask);
}

static inline uint32_t interrupt_enable_read(void) {
  return reg_read(INTERRUPT_CORE0_CPU_INT_ENABLE_REG);
}

static inline uint32_t interrupt_type_read(void) {
  return reg_read(INTERRUPT_CORE0_CPU_INT_TYPE_REG);
}

static inline uint32_t interrupt_priority_read(uint32_t cpu_interrupt_number) {
  return reg_read(INTERRUPT_CPU_INT_PRIO_REG(cpu_interrupt_number));
}

static void interrupt_matrix_clear_all(void) {
  for (uint32_t source = 0; source < PERIPHERAL_INTERRUPT_SOURCE_COUNT; ++source) {
    interrupt_matrix_route(source, CPU_INVALID_INTR_NUM);
  }
}

static void interrupt_route_disconnect_all(void) {
  interrupt_disable_mask(0xFFFFFFFFu);
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, 0xFFFFFFFFu);
  reg_write(RTC_CNTL_INT_ENA_REG, 0u);
  reg_write(RTC_CNTL_INT_CLR_REG, 0xFFFFFFFFu);
  interrupt_matrix_clear_all();
}

static void interrupt_route_connect(const struct irq_route_config *config) {
  interrupt_matrix_route(config->source, config->cpu_interrupt_number);
}

static void interrupt_cpu_line_configure(const struct irq_route_config *config) {
  reg_write(RTC_CNTL_STORE1_REG, config->source);
  reg_write(RTC_CNTL_STORE2_REG, config->cpu_interrupt_number);
  reg_write(RTC_CNTL_STORE0_REG, 0xD085u);
  interrupt_set_priority(config->cpu_interrupt_number, config->priority);
  reg_write(RTC_CNTL_STORE0_REG, 0xD086u);
  interrupt_set_type(config->cpu_interrupt_number, config->type);
  reg_write(RTC_CNTL_STORE0_REG, 0xD087u);
  reg_write(INTERRUPT_CURRENT_CORE_INT_THRESH_REG, 0u);
  reg_write(RTC_CNTL_STORE0_REG, 0xD088u);
}

static void interrupt_selfcheck_dump(const struct irq_route_config *config) {
  reg_write(RTC_CNTL_STORE4_REG, reg_read(INTERRUPT_BASE + (config->source * sizeof(uint32_t))));
  reg_write(RTC_CNTL_STORE5_REG, interrupt_enable_read());
  reg_write(RTC_CNTL_STORE6_REG,
            (interrupt_type_read() & BIT(config->cpu_interrupt_number)) != 0u ? 1u : 0u);
  reg_write(RTC_CNTL_STORE7_REG, interrupt_priority_read(config->cpu_interrupt_number));
}

void interrupts_init(void) {
  reg_write(RTC_CNTL_STORE0_REG, 0xD080u);
  interrupt_route_disconnect_all();
  reg_write(RTC_CNTL_STORE0_REG, 0xD081u);
  interrupt_route_connect(&g_systimer_irq);
  reg_write(RTC_CNTL_STORE0_REG, 0xD082u);
  interrupt_cpu_line_configure(&g_systimer_irq);
  reg_write(RTC_CNTL_STORE0_REG, 0xD083u);
  reg_write(INTERRUPT_CORE0_CPU_INT_ENABLE_REG, BIT(CPU_SYSTIMER_INTR_NUM));
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, BIT(CPU_SYSTIMER_INTR_NUM));
  reg_write(RTC_CNTL_STORE0_REG, 0xD084u);
  interrupt_selfcheck_dump(&g_systimer_irq);
}

void interrupts_init_software_test(void) {
  interrupt_disable_mask(0xFFFFFFFFu);
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, 0xFFFFFFFFu);
  interrupt_matrix_clear_all();
  reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, 0u);
  interrupt_matrix_route(ETS_FROM_CPU_INTR0_SOURCE, CPU_SOFTWARE_TEST_INTR_NUM);
  interrupt_set_priority(CPU_SOFTWARE_TEST_INTR_NUM, 1u);
  interrupt_set_type(CPU_SOFTWARE_TEST_INTR_NUM, INTR_TYPE_LEVEL);
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, BIT(CPU_SOFTWARE_TEST_INTR_NUM));
  reg_write(INTERRUPT_CURRENT_CORE_INT_THRESH_REG, 0u);
  interrupt_enable_mask(BIT(CPU_SOFTWARE_TEST_INTR_NUM));
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}

void interrupts_enable(void) {
  reg_write(RTC_CNTL_STORE0_REG, 0xD100u);
  reg_write(RTC_CNTL_INT_ENA_REG, 0u);
  reg_write(RTC_CNTL_INT_CLR_REG, 0xFFFFFFFFu);
  reg_write(SYSTIMER_INT_CLR_REG, SYSTIMER_TARGET0_INT_CLR_BIT);
  reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, 0u);
  reg_write(INTERRUPT_CORE0_CPU_INT_ENABLE_REG, BIT(CPU_SYSTIMER_INTR_NUM));
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, 0xFFFFFFFFu);
  __asm__ volatile("fence iorw, iorw" ::: "memory");
  reg_write(RTC_CNTL_STORE0_REG, 0xD102u);
  reg_write(RTC_CNTL_STORE1_REG, trap_mtvec_read());
  reg_write(RTC_CNTL_STORE2_REG, reg_read(INTERRUPT_CORE0_CPU_INT_ENABLE_REG));
  reg_write(RTC_CNTL_STORE3_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
  reg_write(RTC_CNTL_STORE4_REG, reg_read(INTERRUPT_BASE + 0x0F8u));
  reg_write(RTC_CNTL_STORE5_REG, reg_read(INTERRUPT_BASE + 0x0FCu));
  reg_write(RTC_CNTL_STORE6_REG, reg_read(SYSTIMER_INT_RAW_REG));
  reg_write(RTC_CNTL_STORE7_REG, reg_read(SYSTIMER_INT_ST_REG));
  reg_write(RTC_CNTL_STORE0_REG, 0xD104u);
  csr_set_bits_mstatus(BIT(3));
  reg_write(RTC_CNTL_STORE0_REG, 0xD103u);
  reg_write(RTC_CNTL_STORE0_REG, 0xD101u);
}

void interrupts_ack(uint32_t cpu_interrupt_number) {
  reg_write(INTERRUPT_CORE0_CPU_INT_CLEAR_REG, BIT(cpu_interrupt_number));
}

void interrupts_trigger_software_test(void) {
  reg_write(RTC_CNTL_STORE1_REG, 0xC100u);
  reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, SYSTEM_CPU_INTR_FROM_CPU_0_BIT);
  __asm__ volatile("fence iorw, iorw" ::: "memory");
  reg_write(RTC_CNTL_STORE1_REG, 0xC101u);
  reg_write(RTC_CNTL_STORE4_REG, reg_read(SYSTEM_CPU_INTR_FROM_CPU_0_REG));
  reg_write(RTC_CNTL_STORE5_REG, reg_read(INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG));
}

void interrupts_trigger_line17_probe(void) {
  reg_write(SYSTEM_CPU_INTR_FROM_CPU_0_REG, SYSTEM_CPU_INTR_FROM_CPU_0_BIT);
  __asm__ volatile("fence iorw, iorw" ::: "memory");
}
