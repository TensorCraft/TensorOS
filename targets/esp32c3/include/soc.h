#pragma once

#include <stdint.h>

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

static inline void reg_write(uint32_t reg, uint32_t value) {
  *(volatile uint32_t *)reg = value;
}

static inline uint32_t reg_read(uint32_t reg) {
  return *(volatile const uint32_t *)reg;
}

static inline void reg_set_bits(uint32_t reg, uint32_t mask) {
  reg_write(reg, reg_read(reg) | mask);
}

static inline void reg_clear_bits(uint32_t reg, uint32_t mask) {
  reg_write(reg, reg_read(reg) & ~mask);
}

#define UART0_BASE      0x60000000u
#define GPIO_BASE       0x60004000u
#define IO_MUX_BASE     0x60009000u
#define SYSTIMER_BASE   0x60023000u
#define SPI2_BASE       0x60024000u
#define LEDC_BASE       0x60019000u
#define RTCCNTL_BASE    0x60008000u
#define SYSTEM_BASE     0x600C0000u
#define INTERRUPT_BASE  0x600C2000u
#define TIMG0_BASE      0x6001F000u
#define TIMG1_BASE      0x60020000u

#define UART_FIFO_REG              (UART0_BASE + 0x0000u)
#define UART_CLKDIV_REG            (UART0_BASE + 0x0014u)
#define UART_STATUS_REG            (UART0_BASE + 0x001Cu)
#define UART_CONF0_REG             (UART0_BASE + 0x0020u)
#define UART_CONF1_REG             (UART0_BASE + 0x0024u)
#define UART_RXFIFO_CNT_MASK       0xFFu
#define UART_TXFIFO_CNT_SHIFT      16u
#define UART_TXFIFO_CNT_MASK       (0xFFu << UART_TXFIFO_CNT_SHIFT)
#define UART_RXFIFO_RST_BIT        BIT(17)
#define UART_TXFIFO_RST_BIT        BIT(18)

#define SYSTIMER_CONF_REG                         (SYSTIMER_BASE + 0x0000u)
#define SYSTIMER_UNIT0_OP_REG                     (SYSTIMER_BASE + 0x0004u)
#define SYSTIMER_UNIT0_LOAD_HI_REG                (SYSTIMER_BASE + 0x000Cu)
#define SYSTIMER_UNIT0_LOAD_LO_REG                (SYSTIMER_BASE + 0x0010u)
#define SYSTIMER_TARGET0_HI_REG                   (SYSTIMER_BASE + 0x001Cu)
#define SYSTIMER_TARGET0_LO_REG                   (SYSTIMER_BASE + 0x0020u)
#define SYSTIMER_TARGET0_CONF_REG                 (SYSTIMER_BASE + 0x0034u)
#define SYSTIMER_UNIT0_VALUE_HI_REG               (SYSTIMER_BASE + 0x0040u)
#define SYSTIMER_UNIT0_VALUE_LO_REG               (SYSTIMER_BASE + 0x0044u)
#define SYSTIMER_COMP0_LOAD_REG                   (SYSTIMER_BASE + 0x0050u)
#define SYSTIMER_UNIT0_LOAD_REG                   (SYSTIMER_BASE + 0x005Cu)
#define SYSTIMER_INT_ENA_REG                      (SYSTIMER_BASE + 0x0064u)
#define SYSTIMER_INT_RAW_REG                      (SYSTIMER_BASE + 0x0068u)
#define SYSTIMER_INT_CLR_REG                      (SYSTIMER_BASE + 0x006Cu)
#define SYSTIMER_INT_ST_REG                       (SYSTIMER_BASE + 0x0070u)
#define SYSTIMER_TARGET0_WORK_EN_BIT              BIT(24)
#define SYSTIMER_TIMER_UNIT0_CORE0_STALL_EN_BIT   BIT(28)
#define SYSTIMER_TIMER_UNIT0_WORK_EN_BIT          BIT(30)
#define SYSTIMER_CLK_EN_BIT                       BIT(31)
#define SYSTIMER_TIMER_UNIT0_VALUE_VALID_BIT      BIT(29)
#define SYSTIMER_TIMER_UNIT0_UPDATE_BIT           BIT(30)
#define SYSTIMER_TIMER_UNIT0_LOAD_BIT             BIT(0)
#define SYSTIMER_TARGET0_PERIOD_MASK              0x03FFFFFFu
#define SYSTIMER_TARGET0_PERIOD_MODE_BIT          BIT(30)
#define SYSTIMER_TARGET0_TIMER_UNIT_SEL_BIT       BIT(31)
#define SYSTIMER_TIMER_COMP0_LOAD_BIT             BIT(0)
#define SYSTIMER_TARGET0_INT_ENA_BIT              BIT(0)
#define SYSTIMER_TARGET0_INT_CLR_BIT              BIT(0)
#define SYSTIMER_TARGET0_INT_ST_BIT               BIT(0)

#define RTC_CNTL_RESET_STATE_REG            (RTCCNTL_BASE + 0x0038u)
#define RTC_CNTL_INT_ENA_REG                (RTCCNTL_BASE + 0x0040u)
#define RTC_CNTL_INT_RAW_REG                (RTCCNTL_BASE + 0x0044u)
#define RTC_CNTL_INT_ST_REG                 (RTCCNTL_BASE + 0x0048u)
#define RTC_CNTL_INT_CLR_REG                (RTCCNTL_BASE + 0x004Cu)
#define RTC_CNTL_STORE0_REG                 (RTCCNTL_BASE + 0x0050u)
#define RTC_CNTL_STORE1_REG                 (RTCCNTL_BASE + 0x0054u)
#define RTC_CNTL_STORE2_REG                 (RTCCNTL_BASE + 0x0058u)
#define RTC_CNTL_STORE3_REG                 (RTCCNTL_BASE + 0x005Cu)
#define RTC_CNTL_WDTCONFIG0_REG             (RTCCNTL_BASE + 0x0090u)
#define RTC_CNTL_WDTWPROTECT_REG            (RTCCNTL_BASE + 0x00A8u)
#define RTC_CNTL_STORE4_REG                 (RTCCNTL_BASE + 0x00B8u)
#define RTC_CNTL_STORE5_REG                 (RTCCNTL_BASE + 0x00BCu)
#define RTC_CNTL_STORE6_REG                 (RTCCNTL_BASE + 0x00C0u)
#define RTC_CNTL_STORE7_REG                 (RTCCNTL_BASE + 0x00C4u)
#define RTC_CNTL_RESET_CAUSE_PROCPU_MASK    0x3Fu
#define RTC_CNTL_WDT_EN_BIT                 BIT(31)
#define RTC_CNTL_WDT_FLASHBOOT_MOD_EN_BIT   BIT(12)

#define SYSTEM_PERIP_CLK_EN0_REG            (SYSTEM_BASE + 0x0010u)
#define SYSTEM_PERIP_RST_EN0_REG            (SYSTEM_BASE + 0x0018u)
#define SYSTEM_CPU_INTR_FROM_CPU_0_REG      (SYSTEM_BASE + 0x0028u)
#define SYSTEM_UART_CLK_EN_BIT              BIT(2)
#define SYSTEM_UART_RST_BIT                 BIT(2)
#define SYSTEM_SPI2_CLK_EN_BIT              BIT(6)
#define SYSTEM_SPI2_RST_BIT                 BIT(6)
#define SYSTEM_SPI2_DMA_CLK_EN_BIT          BIT(22)
#define SYSTEM_SPI2_DMA_RST_BIT             BIT(22)
#define SYSTEM_UART_MEM_CLK_EN_BIT          BIT(24)
#define SYSTEM_UART_MEM_RST_BIT             BIT(24)
#define SYSTEM_LEDC_CLK_EN_BIT              BIT(11)
#define SYSTEM_LEDC_RST_BIT                 BIT(11)
#define SYSTEM_SYSTIMER_CLK_EN_BIT          BIT(29)
#define SYSTEM_SYSTIMER_RST_BIT             BIT(29)
#define SYSTEM_CPU_INTR_FROM_CPU_0_BIT      BIT(0)

#define INTERRUPT_CORE0_SYSTIMER_TARGET0_INT_MAP_REG  (INTERRUPT_BASE + 0x0094u)
#define INTERRUPT_CORE0_CPU_INT_ENABLE_REG            (INTERRUPT_BASE + 0x0104u)
#define INTERRUPT_CORE0_CPU_INT_TYPE_REG              (INTERRUPT_BASE + 0x0108u)
#define INTERRUPT_CORE0_CPU_INT_CLEAR_REG             (INTERRUPT_BASE + 0x010Cu)
#define INTERRUPT_CORE0_CPU_INT_EIP_STATUS_REG        (INTERRUPT_BASE + 0x0110u)
#define INTERRUPT_CPU_INT_PRIO_REG(n)                 (INTERRUPT_BASE + 0x0114u + ((n) * sizeof(uint32_t)))
#define INTERRUPT_CURRENT_CORE_INT_THRESH_REG         (INTERRUPT_BASE + 0x0194u)

#define TIMG_WDTCONFIG0_REG(base)     ((base) + 0x0048u)
#define TIMG_WDTWPROTECT_REG(base)    ((base) + 0x0064u)
#define TIMG_WDT_WKEY_VALUE           0x50D83AA1u
#define TIMG_WDT_DISABLE_VALUE        0u

#define USB_SERIAL_JTAG_BASE                       0x60043000u
#define USB_SERIAL_JTAG_EP1_REG                    (USB_SERIAL_JTAG_BASE + 0x0000u)
#define USB_SERIAL_JTAG_EP1_CONF_REG               (USB_SERIAL_JTAG_BASE + 0x0004u)
#define USB_SERIAL_JTAG_CONF0_REG                  (USB_SERIAL_JTAG_BASE + 0x0018u)
#define USB_SERIAL_JTAG_MISC_CONF_REG              (USB_SERIAL_JTAG_BASE + 0x0044u)
#define USB_SERIAL_JTAG_MEM_CONF_REG               (USB_SERIAL_JTAG_BASE + 0x0048u)
#define USB_SERIAL_JTAG_PHY_SEL_BIT                BIT(0)
#define USB_SERIAL_JTAG_USB_PAD_ENABLE_BIT         BIT(14)
#define USB_SERIAL_JTAG_CLK_EN_BIT                 BIT(0)
#define USB_SERIAL_JTAG_MEM_PD_BIT                 BIT(0)
#define USB_SERIAL_JTAG_MEM_CLK_EN_BIT             BIT(1)
#define USB_SERIAL_JTAG_WR_DONE_BIT                BIT(0)
#define USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_BIT BIT(1)

#define GPIO_OUT_REG               (GPIO_BASE + 0x0004u)
#define GPIO_OUT_W1TS_REG          (GPIO_BASE + 0x0008u)
#define GPIO_OUT_W1TC_REG          (GPIO_BASE + 0x000Cu)
#define GPIO_ENABLE_REG            (GPIO_BASE + 0x0020u)
#define GPIO_ENABLE_W1TS_REG       (GPIO_BASE + 0x0024u)
#define GPIO_ENABLE_W1TC_REG       (GPIO_BASE + 0x0028u)
#define GPIO_IN_REG                (GPIO_BASE + 0x003Cu)
#define GPIO_STATUS_REG            (GPIO_BASE + 0x0044u)
#define GPIO_STATUS_W1TC_REG       (GPIO_BASE + 0x004Cu)
#define GPIO_PCPU_INT_REG          (GPIO_BASE + 0x005Cu)
#define GPIO_PIN_REG(pin)          (GPIO_BASE + 0x0074u + ((pin) * sizeof(uint32_t)))
#define GPIO_FUNC_OUT_SEL_REG(pin) (GPIO_BASE + 0x0554u + ((pin) * sizeof(uint32_t)))
#define GPIO_SIG_GPIO_OUT          0x80u
#define FSPICLK_OUT_IDX            63u
#define FSPID_OUT_IDX              65u
#define GPIO_PIN_INT_ENA_SHIFT     13u
#define GPIO_PIN_INT_ENA_MASK      (0x1Fu << GPIO_PIN_INT_ENA_SHIFT)
#define GPIO_PIN_INT_TYPE_SHIFT    7u
#define GPIO_PIN_INT_TYPE_MASK     (0x7u << GPIO_PIN_INT_TYPE_SHIFT)
#define GPIO_PRO_CPU_INTR_ENA_BIT  BIT(0)

#define IO_MUX_GPIO_REG(pin)       (IO_MUX_BASE + 0x0004u + ((pin) * sizeof(uint32_t)))
#define IO_MUX_FUN_PD_BIT          BIT(7)
#define IO_MUX_FUN_PU_BIT          BIT(8)
#define IO_MUX_FUN_IE_BIT          BIT(9)
#define IO_MUX_MCU_SEL_SHIFT       12u
#define IO_MUX_MCU_SEL_MASK        (0x7u << IO_MUX_MCU_SEL_SHIFT)
#define PIN_FUNC_GPIO_VALUE        (1u << IO_MUX_MCU_SEL_SHIFT)
#define PIN_FUNC_ALT_2_VALUE       (2u << IO_MUX_MCU_SEL_SHIFT)

#define LEDC_LSCH0_CONF0_REG       (LEDC_BASE + 0x0000u)
#define LEDC_LSCH0_HPOINT_REG      (LEDC_BASE + 0x0004u)
#define LEDC_LSCH0_DUTY_REG        (LEDC_BASE + 0x0008u)
#define LEDC_LSCH0_CONF1_REG       (LEDC_BASE + 0x000Cu)
#define LEDC_LSTIMER0_CONF_REG     (LEDC_BASE + 0x00A0u)
#define LEDC_CONF_REG              (LEDC_BASE + 0x00D0u)

#define LEDC_TIMER_SEL_LSCH0_S     0u
#define LEDC_SIG_OUT_EN_LSCH0      BIT(2)
#define LEDC_IDLE_LV_LSCH0         BIT(3)
#define LEDC_PARA_UP_LSCH0         BIT(4)
#define LEDC_DUTY_SCALE_LSCH0_S    0u
#define LEDC_DUTY_CYCLE_LSCH0_S    10u
#define LEDC_DUTY_NUM_LSCH0_S      20u
#define LEDC_DUTY_INC_LSCH0        BIT(30)
#define LEDC_DUTY_START_LSCH0      BIT(31)

#define LEDC_LSTIMER0_DUTY_RES_S   0u
#define LEDC_CLK_DIV_LSTIMER0_S    4u
#define LEDC_LSTIMER0_PAUSE        BIT(22)
#define LEDC_LSTIMER0_RST          BIT(23)
#define LEDC_LSTIMER0_PARA_UP      BIT(25)

#define LEDC_APB_CLK_SEL_S         0u
#define LEDC_CLK_EN                BIT(31)

#define LEDC_LS_SIG_OUT0_IDX       45u

#define SPI_CMD_REG                (SPI2_BASE + 0x0000u)
#define SPI_CTRL_REG               (SPI2_BASE + 0x0008u)
#define SPI_CLOCK_REG              (SPI2_BASE + 0x000Cu)
#define SPI_USER_REG               (SPI2_BASE + 0x0010u)
#define SPI_USER1_REG              (SPI2_BASE + 0x0014u)
#define SPI_USER2_REG              (SPI2_BASE + 0x0018u)
#define SPI_MS_DLEN_REG            (SPI2_BASE + 0x001Cu)
#define SPI_MISC_REG               (SPI2_BASE + 0x0020u)
#define SPI_DOUT_MODE_REG          (SPI2_BASE + 0x002Cu)
#define SPI_DMA_CONF_REG           (SPI2_BASE + 0x0030u)
#define SPI_W0_REG                 (SPI2_BASE + 0x0098u)
#define SPI_SLAVE_REG              (SPI2_BASE + 0x00E0u)
#define SPI_CLK_GATE_REG           (SPI2_BASE + 0x00E8u)

#define SPI_USR_BIT                BIT(24)
#define SPI_UPDATE_BIT             BIT(23)
#define SPI_CLK_EQU_SYSCLK_BIT     BIT(31)
#define SPI_CLKDIV_PRE_SHIFT       18u
#define SPI_CLKCNT_N_SHIFT         12u
#define SPI_CLKCNT_H_SHIFT         6u
#define SPI_CLKCNT_L_SHIFT         0u
#define SPI_USR_COMMAND_BIT        BIT(31)
#define SPI_USR_ADDR_BIT           BIT(30)
#define SPI_USR_DUMMY_BIT          BIT(29)
#define SPI_USR_MISO_BIT           BIT(28)
#define SPI_USR_MOSI_BIT           BIT(27)
#define SPI_CK_OUT_EDGE_BIT        BIT(9)
#define SPI_CS_SETUP_BIT           BIT(7)
#define SPI_CS_HOLD_BIT            BIT(6)
#define SPI_DOUTDIN_BIT            BIT(0)
#define SPI_MST_REMPTY_ERR_END_EN_BIT BIT(27)
#define SPI_USR_COMMAND_BITLEN_SHIFT 28u
#define SPI_MS_DATA_BITLEN_MASK    0x0003FFFFu
#define SPI_CS_KEEP_ACTIVE_BIT     BIT(30)
#define SPI_CK_IDLE_EDGE_BIT       BIT(29)
#define SPI_CK_DIS_BIT             BIT(6)
#define SPI_CS0_DIS_BIT            BIT(0)
#define SPI_BUF_AFIFO_RST_BIT      BIT(30)
#define SPI_RX_AFIFO_RST_BIT       BIT(29)
#define SPI_MST_WFULL_ERR_END_EN_BIT BIT(16)
#define SPI_CLK_EN_BIT             BIT(0)
#define SPI_MST_CLK_ACTIVE_BIT     BIT(1)
#define SPI_MST_CLK_SEL_BIT        BIT(2)

#define ETS_RTC_CORE_INTR_SOURCE         27u
#define ETS_GPIO_INTR_SOURCE             16u
#define ETS_SYSTIMER_TARGET0_INTR_SOURCE 37u
#define ETS_FROM_CPU_INTR0_SOURCE        50u

#define MCAUSE_INTERRUPT_BIT BIT(31)
