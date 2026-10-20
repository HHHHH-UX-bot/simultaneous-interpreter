#ifndef Z7_USB_BOARD_H
#define Z7_USB_BOARD_H
#include <stdint.h>
#include <stdbool.h>
uint32_t usb_read(uintptr_t address);
void usb_write(uintptr_t address,uint32_t value);
void usb_wait(uintptr_t address,uint32_t mask,uint32_t expected,uint32_t error);
__attribute__((noreturn)) void usb_fail(uint32_t error);
bool usb_poll_enabled(void);
void usb_board_init(void);
void usb_port_reset(void);
#endif
