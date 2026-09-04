/*******************************************************************************
 * usb_unused_class_stubs.c — keep TinyUSB's MSC + DFU-mode class drivers OUT of
 * the link (~8.2 KB of internal-DRAM .bss: _mscd_buf 4096 + _dfu_ctx 4116).
 *
 * Why this works: the precompiled libarduino_tinyusb.a was built with every
 * device class enabled, so usbd.c's class-driver table references mscd_* and
 * dfu_moded_* unconditionally, which drags msc_device.c.obj / dfu_device.c.obj
 * (and their big static buffers) into every USB-OTG build. Providing these
 * symbols ourselves satisfies usbd's references first, so the archive members
 * are never pulled in.
 *
 * Why it is safe: an interface class only becomes live if something calls
 * tinyusb_enable_interface() for it — this firmware (and the Arduino core init
 * for a CDC-on-boot build) only ever registers CDC. Verified in the linker map:
 * the ONLY referencer of these objects is usbd.c.obj's driver table, and the
 * wadamesh tree contains no USBMSC / FirmwareMSC / DFU usage. usbd still calls
 * every table entry's init()/reset() on startup and bus reset, which is why
 * these are no-ops rather than absent; open() returning 0 means "interface not
 * claimed" and is never reached anyway (no such interface descriptor exists).
 *
 * Deliberately NOT stubbed: cdcd_* (the companion/console link) and dfu_rtd_*
 * (runtime-DFU is tiny and part of the reset-to-bootloader plumbing).
 *
 * V4 only in practice: the T-Deck ships ARDUINO_USB_MODE=1 (HW-CDC, no TinyUSB
 * device stack linked), where these definitions are simply dead code.
 ******************************************************************************/
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <stdint.h>
#include <stdbool.h>

/* --- MSC class driver (tinyusb usbd_pvt.h driver-table signatures) --- */
void     mscd_init(void) {}
void     mscd_reset(uint8_t rhport) { (void)rhport; }
uint16_t mscd_open(uint8_t rhport, const void* itf_desc, uint16_t max_len) {
  (void)rhport; (void)itf_desc; (void)max_len;
  return 0;   /* interface not claimed (never offered in the descriptor) */
}
bool mscd_control_xfer_cb(uint8_t rhport, uint8_t stage, const void* request) {
  (void)rhport; (void)stage; (void)request;
  return false;
}
bool mscd_xfer_cb(uint8_t rhport, uint8_t ep_addr, uint32_t result, uint32_t xferred_bytes) {
  (void)rhport; (void)ep_addr; (void)result; (void)xferred_bytes;
  return false;
}

/* --- DFU-mode class driver (dfu_rtd_* runtime-DFU intentionally untouched) --- */
void     dfu_moded_init(void) {}
void     dfu_moded_reset(uint8_t rhport) { (void)rhport; }
uint16_t dfu_moded_open(uint8_t rhport, const void* itf_desc, uint16_t max_len) {
  (void)rhport; (void)itf_desc; (void)max_len;
  return 0;
}
bool dfu_moded_control_xfer_cb(uint8_t rhport, uint8_t stage, const void* request) {
  (void)rhport; (void)stage; (void)request;
  return false;
}

#endif /* ESP32 */
