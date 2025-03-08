// apu.c
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "common.h"
#include "lcd_display.h"

#ifndef tight_loop_contents
#define tight_loop_contents() __asm volatile("nop \n")
#endif

#define DBG_ADDR 0x3D

// Function prototypes
void init_hardware();
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length);
void send_ack_to_cpu(uint8_t command_id, ErrorCode error_code);
void play_sound(uint8_t channel, uint8_t sound_id, uint8_t volume);

// LCD display context
lcd_context_t lcd;

int main() {
    // Initialize stdio for print statements
    stdio_init_all();
    printf("TriBoy APU Starting\n");

    // Initialize hardware
    init_hardware();

    // Command processing buffer
    uint8_t cmd_id, length;
    uint8_t data_buffer[256];

    printf("APU entering main loop\n");

    // Main loop
    while (true) {
        // Wait for CS to be asserted by CPU
        if (!gpio_get(APU_CS_PIN)) {

            // Create a buffer for the entire command
            uint8_t cmd_buffer[256];

            // Read first two bytes (guaranteed minimum size)
            spi_read_blocking(APU_SPI_PORT, 0xFF, cmd_buffer, 2);

            // Extract command ID and length
            cmd_id = cmd_buffer[0];
            length = cmd_buffer[1];

            // Read any additional data
            if (length > 2) {
                spi_read_blocking(APU_SPI_PORT, 0xFF, data_buffer, length - 2);
            }

            // Process command after CS is deasserted
            while (!gpio_get(APU_CS_PIN)) {
                tight_loop_contents(); // Wait for CS to be deasserted
            }

            printf("APU: Received command 0x%02X with length %d\n", cmd_id, length);

            // Copy data to data_buffer for processing
            if (length > 2) {
                memcpy(data_buffer, &cmd_buffer[2], length - 2);
            }

            // Process the command
            process_command(cmd_id, data_buffer, length - 2);
        }

        // Small delay to prevent tight loop
        sleep_us(100);
    }

    return 0;
}

void init_hardware() {
    // Initialize SPI in slave mode
    spi_init(APU_SPI_PORT, SPI_FREQUENCY);

    // Configure SPI pins
    gpio_set_function(CPU_APU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_APU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_APU_MISO_PIN, GPIO_FUNC_SPI);

    // Configure CS pin (active low, input from CPU)
    gpio_init(APU_CS_PIN);
    gpio_set_dir(APU_CS_PIN, GPIO_IN);
    gpio_pull_up(APU_CS_PIN);

    // Configure DATA_READY pin (output to CPU)
    gpio_init(APU_DATA_READY_PIN);
    gpio_set_dir(APU_DATA_READY_PIN, GPIO_OUT);
    gpio_put(APU_DATA_READY_PIN, 0);

    // Initialize LCD display with the context and specified address
    bool lcd_init_success = lcd_init(&lcd, APU_DBG_I2C, APU_DBG_SDA_PIN, APU_DBG_SCL_PIN, DBG_ADDR);
    
    if (lcd_init_success) {
        lcd_clear(&lcd);
        lcd_set_cursor(&lcd, 0, 0);
        lcd_string(&lcd, "APU Ready");
        lcd_set_cursor(&lcd, 1, 0);
        lcd_string(&lcd, "Waiting for CMD");
    } else {
        printf("WARNING: LCD initialization failed\n");
    }

    printf("APU hardware initialized\n");
}

void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    // Update LCD with current command
    char cmd_str[17];
    snprintf(cmd_str, sizeof(cmd_str), "CMD: 0x%02X", cmd_id);
    lcd_set_cursor(&lcd, 0, 0);
    lcd_string(&lcd, cmd_str);
    
    switch (cmd_id) {
        case CMD_NOP:
            printf("APU: Processing NOP command\n");
            lcd_set_cursor(&lcd, 1, 0);
            lcd_string(&lcd, "NOP            ");
            // Even for NOP, send an acknowledgment
            send_ack_to_cpu(CMD_NOP, ERR_NONE);
            break;

        case CMD_RESET_AUDIO:
            printf("APU: Processing RESET command\n");
            lcd_set_cursor(&lcd, 1, 0);
            lcd_string(&lcd, "RESET          ");
            // In a real implementation, we would reset APU state here
            send_ack_to_cpu(CMD_RESET_AUDIO, ERR_NONE);
            break;

        case CMD_PLAY_SOUND:
            printf("APU: Processing PLAY_SOUND command\n");
            // Play the requested sound
            if (length >= 2) {
                char sound_info[17];
                snprintf(sound_info, sizeof(sound_info), "PLAY: %d CH: %d", data[1], data[0]);
                lcd_set_cursor(&lcd, 1, 0);
                lcd_string(&lcd, sound_info);
                
                play_sound(data[0], data[1], 255); // Full volume
            }
            send_ack_to_cpu(CMD_PLAY_SOUND, ERR_NONE);
            break;

        default:
            printf("APU: Unknown command 0x%02X\n", cmd_id);
            lcd_set_cursor(&lcd, 1, 0);
            lcd_string(&lcd, "UNKNOWN CMD    ");
            send_ack_to_cpu(cmd_id, ERR_INVALID_COMMAND);
            break;
    }
}

void send_ack_to_cpu(uint8_t command_id, ErrorCode error_code) {
    // Update LCD display for ACK status
    if (error_code == ERR_NONE) {
        lcd_set_cursor(&lcd, 1, 0);
        lcd_string(&lcd, "ACK SENT       ");
    } else {
        char err_str[17];
        snprintf(err_str, sizeof(err_str), "ERR: 0x%02X      ", error_code);
        lcd_set_cursor(&lcd, 1, 0);
        lcd_string(&lcd, err_str);
    }
    
    // Prepare acknowledgment packet
    uint8_t ack_packet[4] = {
        CMD_ACK,      // ACK command ID
        4,            // Packet length
        command_id,   // Original command being acknowledged
        error_code    // Status (0 = success, others = error codes)
    };

    // Wait for CS to be inactive (high)
    while (!gpio_get(APU_CS_PIN)) {
        sleep_us(10);
    }

    // Signal CPU that we have data
    gpio_put(APU_DATA_READY_PIN, 1);

    // Wait for CPU to assert CS (begin reading)
    uint32_t timeout = 10000; // 10ms timeout
    while (gpio_get(APU_CS_PIN) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    // If CS was asserted, send the ACK
    if (timeout > 0) {
        spi_write_blocking(APU_SPI_PORT, ack_packet, 4);
    }

    // Wait for CS to be deasserted
    while (!gpio_get(APU_CS_PIN)) {
        tight_loop_contents();
    }

    // Lower DATA_READY signal
    gpio_put(APU_DATA_READY_PIN, 0);

    printf("APU: Sent ACK for command 0x%02X\n", command_id);
}

void play_sound(uint8_t channel, uint8_t sound_id, uint8_t volume) {
    // This is a simplified version of a sound playback function
    // In a real implementation, this would trigger sound generation
    printf("APU: Playing sound %d on channel %d at volume %d\n", sound_id, channel, volume);

    // Show playing status on LCD
    char play_info[17];
    snprintf(play_info, sizeof(play_info), "Playing...     ");
    lcd_set_cursor(&lcd, 1, 0);
    lcd_string(&lcd, play_info);
    
    // For this example, we just simulate a sound being played
    sleep_ms(10);
}