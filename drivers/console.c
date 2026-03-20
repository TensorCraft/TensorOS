#include "runtime.h"

#define CONSOLE_USB_TX_QUEUE_CAPACITY 1024u

static char g_console_usb_tx_queue[CONSOLE_USB_TX_QUEUE_CAPACITY];
static uint32_t g_console_usb_tx_head;
static uint32_t g_console_usb_tx_tail;
static uint32_t g_console_usb_tx_pending_flush;

static inline uint32_t console_interrupts_save_disable(void) {
  uint32_t mstatus;
  __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(mstatus));
  return mstatus;
}

static inline void console_interrupts_restore(uint32_t mstatus) {
  if ((mstatus & (1u << 3)) != 0u) {
    __asm__ volatile("csrsi mstatus, 8");
  }
}

static uint32_t console_usb_tx_queue_next(uint32_t index) {
  return (index + 1u) % CONSOLE_USB_TX_QUEUE_CAPACITY;
}

static int console_usb_tx_queue_is_empty(void) {
  return g_console_usb_tx_head == g_console_usb_tx_tail;
}

static void console_usb_tx_enqueue(char c) {
  uint32_t next = console_usb_tx_queue_next(g_console_usb_tx_tail);

  if (next == g_console_usb_tx_head) {
    return;
  }

  g_console_usb_tx_queue[g_console_usb_tx_tail] = c;
  g_console_usb_tx_tail = next;
  if (c == '\n') {
    g_console_usb_tx_pending_flush = 1u;
  }
}

static void console_usb_tx_drain(void) {
  while (!console_usb_tx_queue_is_empty()) {
    usb_serial_jtag_putc(g_console_usb_tx_queue[g_console_usb_tx_head]);
    g_console_usb_tx_head = console_usb_tx_queue_next(g_console_usb_tx_head);
  }

  if (g_console_usb_tx_pending_flush != 0u) {
    usb_serial_jtag_flush();
    g_console_usb_tx_pending_flush = 0u;
  }
}

static void console_putc_raw(char c) {
  uart0_putc(c);
  console_usb_tx_enqueue(c);
  console_usb_tx_drain();
}

static void console_write_raw(const char *s) {
  if (s == 0) {
    return;
  }

  while (*s != '\0') {
    console_putc_raw(*s++);
  }
}

static void console_puthex32_raw(uint32_t value) {
  static const char hex[] = "0123456789abcdef";

  uart0_puthex32(value);
  usb_serial_jtag_putc(hex[(value >> 28) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 24) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 20) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 16) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 12) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 8) & 0xFu]);
  usb_serial_jtag_putc(hex[(value >> 4) & 0xFu]);
  usb_serial_jtag_putc(hex[value & 0xFu]);
}

static void console_putdec_u32_raw(uint32_t value) {
  char buf[10];
  int i = 0;

  uart0_putdec_u32(value);

  if (value == 0u) {
    usb_serial_jtag_putc('0');
    return;
  }

  while (value > 0u) {
    buf[i++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  while (i > 0) {
    usb_serial_jtag_putc(buf[--i]);
  }
}

void console_init(void) {
  uart0_init();
  usb_serial_jtag_init();
  g_console_usb_tx_head = 0u;
  g_console_usb_tx_tail = 0u;
  g_console_usb_tx_pending_flush = 0u;
}

void console_putc(char c) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_putc_raw(c);
  console_interrupts_restore(mstatus);
}

void console_write(const char *s) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_write_raw(s);
  console_interrupts_restore(mstatus);
}

void console_puts(const char *s) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_write_raw(s);
  console_putc_raw('\n');
  console_interrupts_restore(mstatus);
}

void console_puthex32(uint32_t value) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_puthex32_raw(value);
  console_interrupts_restore(mstatus);
}

void console_putdec_u32(uint32_t value) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_putdec_u32_raw(value);
  console_interrupts_restore(mstatus);
}

void console_log(const char *tag, const char *message) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_putc_raw('[');
  console_write_raw(tag);
  console_putc_raw(']');
  console_putc_raw(' ');
  console_write_raw(message);
  console_putc_raw('\n');
  console_interrupts_restore(mstatus);
}

void console_log_u32(const char *tag, const char *label, uint32_t value) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_putc_raw('[');
  console_write_raw(tag);
  console_putc_raw(']');
  console_putc_raw(' ');
  console_write_raw(label);
  console_putdec_u32_raw(value);
  console_putc_raw('\n');
  console_interrupts_restore(mstatus);
}

void console_log_hex32(const char *tag, const char *label, uint32_t value) {
  uint32_t mstatus = console_interrupts_save_disable();
  console_putc_raw('[');
  console_write_raw(tag);
  console_putc_raw(']');
  console_putc_raw(' ');
  console_write_raw(label);
  console_write_raw("0x");
  console_puthex32_raw(value);
  console_putc_raw('\n');
  console_interrupts_restore(mstatus);
}
