#include "runtime.h"
#include "soc.h"

/*
 * Polled USB Serial/JTAG TX path with packet buffering. The ROM boot path has
 * already enumerated the device as a USB CDC console, so the kernel only needs
 * to push EP1 packets and close each transaction correctly.
 */
#define USB_SERIAL_TIMEOUT_ITERATIONS 500000u
#define USB_SERIAL_PACKET_SIZE        64u

static uint8_t g_tx_packet[USB_SERIAL_PACKET_SIZE];
static uint32_t g_tx_packet_len;

static int usb_serial_jtag_wait_writable(void) {
  uint32_t spins = 0u;
  while ((reg_read(USB_SERIAL_JTAG_EP1_CONF_REG) & USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_BIT) == 0u) {
    if (spins++ >= USB_SERIAL_TIMEOUT_ITERATIONS) {
      return 0;
    }
  }
  return 1;
}

static void usb_serial_jtag_flush_zlp_if_needed(uint32_t packet_len) {
  if (packet_len != USB_SERIAL_PACKET_SIZE) {
    return;
  }

  if (!usb_serial_jtag_wait_writable()) {
    return;
  }

  reg_set_bits(USB_SERIAL_JTAG_EP1_CONF_REG, USB_SERIAL_JTAG_WR_DONE_BIT);
}

static void usb_serial_jtag_flush_packet(void) {
  uint32_t packet_len = g_tx_packet_len;
  if (packet_len == 0u) {
    return;
  }

  if (!usb_serial_jtag_wait_writable()) {
    g_tx_packet_len = 0u;
    return;
  }

  for (uint32_t i = 0; i < packet_len; ++i) {
    reg_write(USB_SERIAL_JTAG_EP1_REG, g_tx_packet[i]);
  }

  reg_set_bits(USB_SERIAL_JTAG_EP1_CONF_REG, USB_SERIAL_JTAG_WR_DONE_BIT);
  g_tx_packet_len = 0u;
  usb_serial_jtag_flush_zlp_if_needed(packet_len);
}

void usb_serial_jtag_init(void) {
  /*
   * Keep the runtime path conservative: rely on the ROM-configured native USB
   * device state and only ensure the register/memory clocks are accessible.
   */
  reg_clear_bits(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PHY_SEL_BIT);
  reg_set_bits(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE_BIT);
  reg_set_bits(USB_SERIAL_JTAG_MISC_CONF_REG, USB_SERIAL_JTAG_CLK_EN_BIT);
  reg_clear_bits(USB_SERIAL_JTAG_MEM_CONF_REG, USB_SERIAL_JTAG_MEM_PD_BIT);
  reg_set_bits(USB_SERIAL_JTAG_MEM_CONF_REG, USB_SERIAL_JTAG_MEM_CLK_EN_BIT);
  g_tx_packet_len = 0u;
}

void usb_serial_jtag_putc(char c) {
  g_tx_packet[g_tx_packet_len++] = (uint8_t)c;
  if (g_tx_packet_len == USB_SERIAL_PACKET_SIZE) {
    usb_serial_jtag_flush_packet();
  }
}

void usb_serial_jtag_flush(void) {
  usb_serial_jtag_flush_packet();
}
