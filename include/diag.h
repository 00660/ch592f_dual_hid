#ifndef DUAL_HID_DIAG_H
#define DUAL_HID_DIAG_H

#include <stdint.h>

void diag_uart_init(void);
void diag_entry(void);
void diag_flush(void);
void diag_set_quiet(uint8_t quiet);
uint8_t diag_is_quiet(void);
void diag_putc(char value);
int diag_printf(const char *format, ...);
void diag_fault(uint32_t cause, uint32_t pc, uint32_t value, uint32_t stack);

#endif
