// lcd_display.h
#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Initialize the LCD display
void lcd_init(i2c_inst_t *i2c_port, uint sda_pin, uint scl_pin);

// Clear the LCD display
void lcd_clear(void);

// Set cursor position
void lcd_set_cursor(int line, int position);

// Display a string at the current cursor position
void lcd_string(const char *s);

// Helper functions to show commands
void lcd_show_command(const char *cmd_name, uint8_t cmd_value);
void lcd_show_status(const char *status);

#endif // LCD_DISPLAY_H