// gpu.c
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"

#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "common.h"
#include "lcd_display.h"

#ifndef tight_loop_contents
#define tight_loop_contents() __asm volatile("nop \n")
#endif

#define DBG_ADDR 0x23

// Function prototypes
void init_hardware();
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length);
void send_ack_to_cpu(uint8_t command_id, ErrorCode error_code);
void send_vsync_to_cpu();
void core1_vsync_simulator();

// Global state variables
bool vblank_callback_enabled = false;
volatile bool should_exit = false;
bool spi_vsync_notification_enabled = false;

int main() {
    // Initialize stdio for print statements
    stdio_init_all();
    printf("TriBoy GPU Starting\n");

    // Initialize hardware
    init_hardware();

    // Launch Core 1 for VSYNC simulation
    multicore_launch_core1(core1_vsync_simulator);

    // Command processing buffer
    uint8_t cmd_id, length;
    uint8_t data_buffer[256];

    printf("GPU entering main loop\n");

    // Main loop
    while (!should_exit) {
        // Wait for CS to be asserted by CPU
        if (!gpio_get(GPU_CS_PIN)) {
            // Create a buffer for the entire command
            uint8_t cmd_buffer[256];

            // Read first two bytes (guaranteed minimum size)
            spi_read_blocking(GPU_SPI_PORT, 0xFF, cmd_buffer, 2);

            // Extract command ID and length
            cmd_id = cmd_buffer[0];
            length = cmd_buffer[1];

            // Read any additional data if needed
            if (length > 2) {
                spi_read_blocking(GPU_SPI_PORT, 0xFF, &cmd_buffer[2], length - 2);
            }

            // Process command after CS is deasserted
            while (!gpio_get(GPU_CS_PIN)) {
                tight_loop_contents();
            }

            printf("GPU: Received command 0x%02X with length %d\n", cmd_id, length);

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
    spi_init(GPU_SPI_PORT, SPI_FREQUENCY);

    // Configure SPI pins
    gpio_set_function(CPU_GPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_GPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_GPU_MISO_PIN, GPIO_FUNC_SPI);

    // Configure CS pin (active low, input from CPU)
    gpio_init(GPU_CS_PIN);
    gpio_set_dir(GPU_CS_PIN, GPIO_IN);
    gpio_pull_up(GPU_CS_PIN);

    // Configure DATA_READY pin (output to CPU)
    gpio_init(GPU_DATA_READY_PIN);
    gpio_set_dir(GPU_DATA_READY_PIN, GPIO_OUT);
    gpio_put(GPU_DATA_READY_PIN, 0);

    // Configure VSYNC pin (output to CPU)
    gpio_init(GPU_VSYNC_PIN);
    gpio_set_dir(GPU_VSYNC_PIN, GPIO_OUT);
    gpio_put(GPU_VSYNC_PIN, 1);

    // Initialize LCD display
    lcd_init(GPU_DBG_I2C, GPU_DBG_SDA_PIN, GPU_DBG_SCL_PIN);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("GPU Ready");
    lcd_set_cursor(1, 0);
    lcd_string("Waiting for CMD");

    printf("GPU hardware initialized\n");
}

void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    switch (cmd_id) {
        case CMD_NOP:
            printf("GPU: Processing NOP command\n");
            // Even for NOP, send an acknowledgment
            send_ack_to_cpu(CMD_NOP, ERR_NONE);
            break;

        case CMD_RESET_GPU:
            printf("GPU: Processing RESET command\n");
            // In a real implementation, we would reset GPU state here
            send_ack_to_cpu(CMD_RESET_GPU, ERR_NONE);
            break;

        case CMD_ENABLE_SPI_VSYNC:
            spi_vsync_notification_enabled = true;
            send_ack_to_cpu(CMD_ENABLE_SPI_VSYNC, ERR_NONE);
            break;

        case CMD_DISABLE_SPI_VSYNC:
            spi_vsync_notification_enabled = false;
            send_ack_to_cpu(CMD_DISABLE_SPI_VSYNC, ERR_NONE);
            break;

        case CMD_SET_VSYNC_CALLBACK:
            printf("GPU: Processing SET_VSYNC_CALLBACK command\n");
            // Enable/disable VSYNC callback
            vblank_callback_enabled = (data[0] != 0);
            send_ack_to_cpu(CMD_SET_VSYNC_CALLBACK, ERR_NONE);
            break;

        case CMD_VSYNC_WAIT:
            printf("GPU: Processing VSYNC_WAIT command\n");
            // In a real implementation, the response would be sent at next VSYNC
            // For this example, we'll just acknowledge now
            send_ack_to_cpu(CMD_VSYNC_WAIT, ERR_NONE);
            break;

        default:
            printf("GPU: Unknown command 0x%02X\n", cmd_id);
            send_ack_to_cpu(cmd_id, ERR_INVALID_COMMAND);
            break;
    }
}

void send_ack_to_cpu(uint8_t command_id, ErrorCode error_code) {
    // Prepare acknowledgment packet
    uint8_t ack_packet[4] = {
        CMD_ACK,      // ACK command ID
        4,            // Packet length
        command_id,   // Original command being acknowledged
        error_code    // Status (0 = success, others = error codes)
    };

    // Wait for CS to be inactive (high)
    while (!gpio_get(GPU_CS_PIN)) {
        sleep_us(10);
    }

    // Signal CPU that we have data
    gpio_put(GPU_DATA_READY_PIN, 1);

    // Wait for CPU to assert CS (begin reading)
    uint32_t timeout = 10000; // 10ms timeout
    while (gpio_get(GPU_CS_PIN) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    // If CS was asserted, send the ACK
    if (timeout > 0) {
        spi_write_blocking(GPU_SPI_PORT, ack_packet, 4);
    }

    // Wait for CS to be deasserted
    while (!gpio_get(GPU_CS_PIN)) {
        tight_loop_contents();
    }

    // Lower DATA_READY signal
    gpio_put(GPU_DATA_READY_PIN, 0);

    printf("GPU: Sent ACK for command 0x%02X\n", command_id);
}

void send_vsync_to_cpu() {
    if (!vblank_callback_enabled) {
        return;
    }

    // Method 1: GPIO interrupt (primary method)
    gpio_put(GPU_VSYNC_PIN, 0);  // Active low
    sleep_us(10);                 // Short pulse
    gpio_put(GPU_VSYNC_PIN, 1);   // Back to inactive

    // Method 2: SPI notification (backup, if enabled)
    if (spi_vsync_notification_enabled) {
        // Prepare VSYNC packet
        uint8_t vsync_packet[4] = {
            CMD_VSYNC,    // VSYNC command ID
            4,            // Packet length
            0,            // No command reference
            ERR_NONE      // No status
        };

        // Signal CPU that we have data
        gpio_put(GPU_DATA_READY_PIN, 1);

        // Wait for CPU to assert CS (begin reading) with timeout
        uint32_t timeout = 10000; // 10ms timeout
        while (gpio_get(GPU_CS_PIN) && timeout > 0) {
            sleep_us(1);
            timeout--;
        }

        // If CS was asserted, send the VSYNC notification
        if (timeout > 0) {
            spi_write_blocking(GPU_SPI_PORT, vsync_packet, 4);
        }

        // Wait for CS to be deasserted
        while (!gpio_get(GPU_CS_PIN) && timeout > 0) {
            sleep_us(1);
            timeout--;
        }

        // Lower DATA_READY signal
        gpio_put(GPU_DATA_READY_PIN, 0);
    }

    printf("GPU: Sent VSYNC notification to CPU\n");
}

// VSYNC simulation running on Core 1
void core1_vsync_simulator() {
    printf("GPU Core 1: VSYNC simulator started\n");

    while (!should_exit) {
        // Simulate a 60Hz VSYNC signal (approximately)
        sleep_ms(16);  // ~60fps

        // Send VSYNC signal to CPU
        send_vsync_to_cpu();
    }
}