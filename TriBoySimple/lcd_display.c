// lcd_display.c
#include <stdio.h>
#include <string.h>
#include "lcd_display.h"

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYLEFT 0x02

// flags for display and cursor control
#define LCD_BLINKON 0x01
#define LCD_CURSORON 0x02
#define LCD_DISPLAYON 0x04

// flags for display and cursor shift
#define LCD_MOVERIGHT 0x04
#define LCD_DISPLAYMOVE 0x08

// flags for function set
#define LCD_5x10DOTS 0x04
#define LCD_2LINE 0x08
#define LCD_8BITMODE 0x10

// flag for backlight control
#define LCD_BACKLIGHT 0x08

#define LCD_ENABLE_BIT 0x04

// By default these LCD display drivers are on bus address 0x27
static int addr = 0x23;
static i2c_inst_t *i2c_port = NULL;

// Modes for lcd_send_byte
#define LCD_CHARACTER  1
#define LCD_COMMAND    0

#define MAX_LINES      2
#define MAX_CHARS      16

/* Quick helper function for single byte transfers */
static void i2c_write_byte(uint8_t val) {
    if (i2c_port != NULL) {
        i2c_write_blocking(i2c_port, addr, &val, 1, false);
    }
}

static void lcd_toggle_enable(uint8_t val) {
    // Toggle enable pin on LCD display
    // We cannot do this too quickly or things don't work
    #define DELAY_US 600
    sleep_us(DELAY_US);
    i2c_write_byte(val | LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
    i2c_write_byte(val & ~LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
}

// The display is sent a byte as two separate nibble transfers
static void lcd_send_byte(uint8_t val, int mode) {
    uint8_t high = mode | (val & 0xF0) | LCD_BACKLIGHT;
    uint8_t low = mode | ((val << 4) & 0xF0) | LCD_BACKLIGHT;

    i2c_write_byte(high);
    lcd_toggle_enable(high);
    i2c_write_byte(low);
    lcd_toggle_enable(low);
}

void lcd_clear(void) {
    lcd_send_byte(LCD_CLEARDISPLAY, LCD_COMMAND);
    sleep_ms(2); // This command takes a long time
}

// go to location on LCD
void lcd_set_cursor(int line, int position) {
    int val = (line == 0) ? 0x80 + position : 0xC0 + position;
    lcd_send_byte(val, LCD_COMMAND);
}

static inline void lcd_char(char val) {
    lcd_send_byte(val, LCD_CHARACTER);
}

void lcd_string(const char *s) {
    while (*s) {
        lcd_char(*s++);
    }
}

void lcd_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin) {
    i2c_port = i2c;
    
    // Initialize I2C with debugging
    printf("LCD: Starting initialization\n");
    i2c_init(i2c, 100 * 1000);
    printf("LCD: I2C initialized\n");
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    printf("LCD: GPIO functions set\n");
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);
    printf("LCD: Pull-ups enabled\n");
    
    // Test if device is present
    printf("LCD: Testing for device at address 0x%02X\n", addr);
    uint8_t rxdata;
    int ret = i2c_read_blocking(i2c, addr, &rxdata, 1, false);
    if (ret < 0) {
        printf("LCD: No device found at address 0x%02X (error %d)\n", addr, ret);
        // Maybe try alternative addresses
        for (int alt_addr = 0x20; alt_addr <= 0x27; alt_addr++) {
            if (alt_addr == addr) continue;
            ret = i2c_read_blocking(i2c, alt_addr, &rxdata, 1, false);
            if (ret >= 0) {
                printf("LCD: Found device at alternative address 0x%02X\n", alt_addr);
                addr = alt_addr;
                break;
            }
        }
        if (ret < 0) {
            printf("LCD: No LCD device found, initialization failed\n");
            return; // Exit initialization if no device found
        }
    } else {
        printf("LCD: Device found at address 0x%02X\n", addr);
    }
    
    sleep_ms(50); // Wait for LCD to initialize
    printf("LCD: Starting display initialization sequence\n");
    
    // Initialize display
    printf("LCD: Sending init sequence 1/3\n");
    lcd_send_byte(0x03, LCD_COMMAND);
    printf("LCD: Sending init sequence 2/3\n");
    lcd_send_byte(0x03, LCD_COMMAND);
    printf("LCD: Sending init sequence 3/3\n");
    lcd_send_byte(0x03, LCD_COMMAND);
    printf("LCD: Sending 4-bit mode command\n");
    lcd_send_byte(0x02, LCD_COMMAND);

    printf("LCD: Setting entry mode\n");
    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND);
    printf("LCD: Setting function\n");
    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND);
    printf("LCD: Setting display control\n");
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND);
    printf("LCD: Clearing display\n");
    lcd_clear();
    printf("LCD: Initialization complete\n");
}

// Helper function to display a command name and value
void lcd_show_command(const char *cmd_name, uint8_t cmd_value) {
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string(cmd_name);
    
    char hex_value[8];
    snprintf(hex_value, sizeof(hex_value), "0x%02X", cmd_value);
    lcd_set_cursor(1, 0);
    lcd_string(hex_value);
}

// Helper function to display status
void lcd_show_status(const char *status) {
    lcd_set_cursor(1, 0);
    lcd_string("                "); // Clear line
    lcd_set_cursor(1, 0);
    lcd_string(status);
}