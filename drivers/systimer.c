#include "runtime.h"
#include "soc.h"

static int g_systimer_initialized;
static uint32_t g_systimer_read_timeout_count;

#define SYSTIMER_READ_TIMEOUT_ITERATIONS 100000u

void systimer_unit0_init(void) {
    reg_set_bits(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_SYSTIMER_CLK_EN_BIT);
    reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_SYSTIMER_RST_BIT);

    uint32_t conf = reg_read(SYSTIMER_CONF_REG);
    conf |= SYSTIMER_CLK_EN_BIT | SYSTIMER_TIMER_UNIT0_WORK_EN_BIT;
    conf &= ~SYSTIMER_TIMER_UNIT0_CORE0_STALL_EN_BIT;
    reg_write(SYSTIMER_CONF_REG, conf);
    reg_write(SYSTIMER_UNIT0_LOAD_HI_REG, 0u);
    reg_write(SYSTIMER_UNIT0_LOAD_LO_REG, 0u);
    reg_write(SYSTIMER_UNIT0_LOAD_REG, SYSTIMER_TIMER_UNIT0_LOAD_BIT);
    g_systimer_initialized = 1;
}

uint64_t systimer_unit0_read(void) {
    uint32_t spins = 0u;

    reg_write(SYSTIMER_UNIT0_OP_REG, SYSTIMER_TIMER_UNIT0_UPDATE_BIT);
    while ((reg_read(SYSTIMER_UNIT0_OP_REG) & SYSTIMER_TIMER_UNIT0_VALUE_VALID_BIT) == 0u) {
        if (++spins >= SYSTIMER_READ_TIMEOUT_ITERATIONS) {
            ++g_systimer_read_timeout_count;
            return 0u;
        }
    }

    uint32_t hi = reg_read(SYSTIMER_UNIT0_VALUE_HI_REG) & 0x000FFFFFu;
    uint32_t lo = reg_read(SYSTIMER_UNIT0_VALUE_LO_REG);
    return (((uint64_t)hi) << 32) | (uint64_t)lo;
}

int systimer_unit0_is_initialized(void) {
    return g_systimer_initialized;
}

uint32_t systimer_unit0_timeout_count(void) {
    return g_systimer_read_timeout_count;
}
