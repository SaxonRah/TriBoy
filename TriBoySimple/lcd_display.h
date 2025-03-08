// lcd_display.h
#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"

// LCD context structure to hold state
typedef struct {
    i2c_inst_t *i2c_port;
    uint8_t addr;
} lcd_context_t;

// Initialize the LCD display with configurable address
bool lcd_init(lcd_context_t *lcd, i2c_inst_t *i2c_port, uint sda_pin, uint scl_pin, uint8_t lcd_addr);

// Clear the LCD display
void lcd_clear(lcd_context_t *lcd);

// Set cursor position
void lcd_set_cursor(lcd_context_t *lcd, int line, int position);

// Display a string at the current cursor position
void lcd_string(lcd_context_t *lcd, const char *s);

// Helper functions to show commands
void lcd_show_command(lcd_context_t *lcd, const char *cmd_name, uint8_t cmd_value);
void lcd_show_status(lcd_context_t *lcd, const char *status);

#endif // LCD_DISPLAY_H