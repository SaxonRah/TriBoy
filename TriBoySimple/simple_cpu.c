// cpu.c
//#define SYS_CLK_MHZ 200
#define PICO_USE_FASTEST_SUPPORTED_CLOCK 1

#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "common.h"

// Variables for VSYNC handling
volatile bool vsync_received = false;
bool vsync_callback_enabled = false;

// Function prototypes
void init_hardware();
void send_command_to_gpu(uint8_t cmd_id, uint8_t length, const uint8_t* data);
void send_command_to_apu(uint8_t cmd_id, uint8_t length, const uint8_t* data);
void process_gpu_response();
void process_apu_response();
void vsync_callback(uint gpio, uint32_t events);

int main() {
    // Initialize stdio for print statements
    stdio_init_all();
    printf("TriBoy CPU Starting\n");

    // Initialize hardware
    init_hardware();

    // Simple test data
    uint8_t test_data[2] = {42, 123};

    // Main loop
    while (true) {
        // Send command to GPU
        printf("CPU: Sending command to GPU\n");
        send_command_to_gpu(CMD_NOP, 3, test_data);

        // Send command to APU
        printf("CPU: Sending command to APU\n");
        send_command_to_apu(CMD_PLAY_SOUND, 3, test_data);

        // Enable VSYNC callback from GPU
        printf("CPU: Enabling VSYNC callback\n");
        uint8_t enable_data[1] = {1};
        send_command_to_gpu(CMD_SET_VSYNC_CALLBACK, 2, enable_data);
        vsync_callback_enabled = true;

        // Wait for responses
        uint32_t timeout = time_us_32() + 1000000; // 1 second timeout
        while (time_us_32() < timeout) {
            // Check if GPU has data ready
            if (gpio_get(GPU_DATA_READY_PIN)) {
                process_gpu_response();
            }

            // Check if APU has data ready
            if (gpio_get(APU_DATA_READY_PIN)) {
                process_apu_response();
            }

            // Check for VSYNC
            if (vsync_received) {
                printf("CPU: Received VSYNC from GPU\n");
                vsync_received = false;
                break;
            }

            sleep_ms(10);
        }

        // Display timeout message if no VSYNC received
        if (!vsync_received && vsync_callback_enabled) {
            printf("CPU: Timeout waiting for VSYNC\n");
        }

        // Wait before next cycle
        sleep_ms(2000);
    }

    return 0;
}

void init_hardware() {
    // Initialize SPI for GPU communication
    spi_init(CPU_GPU_SPI_PORT, SPI_FREQUENCY);
    gpio_set_function(CPU_GPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_GPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_GPU_MISO_PIN, GPIO_FUNC_SPI);

    gpio_init(GPU_CS_PIN);
    gpio_set_dir(GPU_CS_PIN, GPIO_OUT);
    gpio_put(GPU_CS_PIN, 1); // Deselect by default

    // Initialize SPI for APU communication
    spi_init(APU_SPI_PORT, SPI_FREQUENCY);
    gpio_set_function(CPU_APU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_APU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_APU_MISO_PIN, GPIO_FUNC_SPI);

    gpio_init(APU_CS_PIN);
    gpio_set_dir(APU_CS_PIN, GPIO_OUT);
    gpio_put(APU_CS_PIN, 1); // Deselect by default

    // Initialize DATA_READY pins as inputs with pull-downs
    gpio_init(GPU_DATA_READY_PIN);
    gpio_set_dir(GPU_DATA_READY_PIN, GPIO_IN);
    gpio_pull_down(GPU_DATA_READY_PIN);

    gpio_init(APU_DATA_READY_PIN);
    gpio_set_dir(APU_DATA_READY_PIN, GPIO_IN);
    gpio_pull_down(APU_DATA_READY_PIN);

    // Setup VSYNC pin with interrupt
    gpio_init(GPU_VSYNC_PIN);
    gpio_set_dir(GPU_VSYNC_PIN, GPIO_IN);
    gpio_pull_up(GPU_VSYNC_PIN);

    // Configure interrupt for VSYNC (falling edge)
    gpio_set_irq_enabled_with_callback(GPU_VSYNC_PIN, GPIO_IRQ_EDGE_FALL, true, &vsync_callback);

    printf("CPU hardware initialized\n");
}

void send_command_to_gpu(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    uint8_t cmd_buffer[256]; // Buffer for the command

    // Prepare command data
    cmd_buffer[0] = cmd_id;   // Command ID
    cmd_buffer[1] = length;   // Command length (including ID and length bytes)

    // Check if GPU is ready (CS should be deasserted, DATA_READY should be low)
    uint32_t timeout = 1000; // 1ms timeout
    while ((gpio_get(GPU_DATA_READY_PIN) || !gpio_get(GPU_CS_PIN)) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("CPU: Timeout waiting for GPU to be ready\n");
        return; // Skip this command
    }

    // Copy command data if any
    if (length > 2 && data != NULL) {
        memcpy(&cmd_buffer[2], data, length - 2);
    }

    // Send command to GPU
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(CPU_GPU_SPI_PORT, cmd_buffer, length);
    gpio_put(GPU_CS_PIN, 1);
}

void send_command_to_apu(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    uint8_t cmd_buffer[256]; // Buffer for the command

    // Prepare command data
    cmd_buffer[0] = cmd_id;   // Command ID
    cmd_buffer[1] = length;   // Command length (including ID and length bytes)

    // Check if APU is ready (CS should be deasserted, DATA_READY should be low)
    uint32_t timeout = 1000; // 1ms timeout
    while ((gpio_get(APU_DATA_READY_PIN) || !gpio_get(APU_CS_PIN)) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("CPU: Timeout waiting for APU to be ready\n");
        return; // Skip this command
    }

    // Copy command data if any
    if (length > 2 && data != NULL) {
        memcpy(&cmd_buffer[2], data, length - 2);
    }

    // Send command to APU
    gpio_put(APU_CS_PIN, 0); // Select APU
    spi_write_blocking(APU_SPI_PORT, cmd_buffer, length);
    gpio_put(APU_CS_PIN, 1); // Deselect APU
}

void process_gpu_response() {
    uint8_t response[4];

    // Read response from GPU
    gpio_put(GPU_CS_PIN, 0); // Select GPU
    spi_read_blocking(GPU_SPI_PORT, 0xFF, response, 4);
    gpio_put(GPU_CS_PIN, 1); // Deselect GPU

    // Wait for DATA_READY to go low (acknowledgment of processing)
    uint32_t timeout = 5000; // 5ms timeout
    while (gpio_get(GPU_DATA_READY_PIN) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("CPU: Timeout waiting for GPU DATA_READY to go low\n");
    }

    // Process response based on type
    if (response[0] == CMD_ACK) {
        printf("CPU: Received ACK from GPU for command 0x%02X\n", response[2]);
    } else if (response[0] == CMD_VSYNC) {
        printf("CPU: Received VSYNC notification from GPU\n");
        vsync_received = true;
    } else {
        printf("CPU: Received unknown response from GPU: 0x%02X\n", response[0]);
    }
}

void process_apu_response() {
    uint8_t response[4];

    // Read response from APU
    gpio_put(APU_CS_PIN, 0); // Select APU
    spi_read_blocking(APU_SPI_PORT, 0xFF, response, 4); // Send dummy byte to receive data
    gpio_put(APU_CS_PIN, 1); // Deselect APU

    // Wait for DATA_READY to go low (acknowledgment of processing)
    uint32_t timeout = 5000; // 5ms timeout
    while (gpio_get(APU_DATA_READY_PIN) && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("CPU: Timeout waiting for APU DATA_READY to go low\n");
    }

    // Process response based on type
    if (response[0] == CMD_ACK) {
        printf("CPU: Received ACK from APU for command 0x%02X\n", response[2]);
    } else {
        printf("CPU: Received unknown response from APU: 0x%02X\n", response[0]);
    }
}

void vsync_callback(uint gpio, uint32_t events) {
    if (gpio == GPU_VSYNC_PIN && vsync_callback_enabled) {
        vsync_received = true;
    }
}