#include "runtime.h"
#include "soc.h"

#define UART_FIFO_CAPACITY 128u
#define APB_CLK_HZ         80000000u
#define UART_BAUD_RATE     115200u

void uart0_init(void) {
    reg_set_bits(SYSTEM_PERIP_CLK_EN0_REG, SYSTEM_UART_CLK_EN_BIT | SYSTEM_UART_MEM_CLK_EN_BIT);
    reg_set_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_UART_RST_BIT);
    reg_set_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_UART_MEM_RST_BIT);
    reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_UART_RST_BIT);
    reg_clear_bits(SYSTEM_PERIP_RST_EN0_REG, SYSTEM_UART_MEM_RST_BIT);

    reg_write(UART_CLKDIV_REG, APB_CLK_HZ / UART_BAUD_RATE);

    reg_write(UART_CONF0_REG, UART_TXFIFO_RST_BIT | UART_RXFIFO_RST_BIT);
    reg_write(UART_CONF0_REG, 0u);
    reg_write(UART_CONF1_REG, 0u);
}

void uart0_putc(char c) {
    while (((reg_read(UART_STATUS_REG) & UART_TXFIFO_CNT_MASK) >> UART_TXFIFO_CNT_SHIFT) >= UART_FIFO_CAPACITY) {
    }
    reg_write(UART_FIFO_REG, (uint8_t)c);
}

void uart0_puts(const char *s) {
    while (*s != '\0') {
        if (*s == '\n') {
            uart0_putc('\r');
        }
        uart0_putc(*s++);
    }
}

void uart0_puthex32(uint32_t value) {
    static const char hex[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        uart0_putc(hex[(value >> shift) & 0xFu]);
    }
}

void uart0_putdec_u32(uint32_t value) {
    char buf[10];
    int i = 0;

    if (value == 0u) {
        uart0_putc('0');
        return;
    }

    while (value > 0u) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0) {
        uart0_putc(buf[--i]);
    }
}

int uart0_try_getc(char *c) {
    if ((c == 0) || ((reg_read(UART_STATUS_REG) & UART_RXFIFO_CNT_MASK) == 0u)) {
        return 0;
    }

    *c = (char)(reg_read(UART_FIFO_REG) & 0xFFu);
    return 1;
}
