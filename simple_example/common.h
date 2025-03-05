// triboy_common.h
#ifndef TRIBOY_COMMON_H
#define TRIBOY_COMMON_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

// SPI configuration
#define SPI_FREQUENCY 8000000  // 8MHz
#define CPU_SPI_PORT spi0
#define GPU_SPI_PORT spi0
#define APU_SPI_PORT spi1

// PIN definitions
// SPI0 for CPU to GPU communication
#define CPU_GPU_SCK_PIN 2
#define CPU_GPU_MOSI_PIN 3
#define CPU_GPU_MISO_PIN 4
#define GPU_CS_PIN 5
#define GPU_DATA_READY_PIN 6
#define GPU_VSYNC_PIN 7

// SPI1 for CPU to APU communication
#define CPU_APU_SCK_PIN 10
#define CPU_APU_MOSI_PIN 11
#define CPU_APU_MISO_PIN 12
#define APU_CS_PIN 13
#define APU_DATA_READY_PIN 14

// Command IDs
#define CMD_NOP 0x00
#define CMD_RESET_GPU 0x01
#define CMD_RESET_AUDIO 0x01
#define CMD_SET_VSYNC_CALLBACK 0x03
#define CMD_VSYNC_WAIT 0x04
#define CMD_PLAY_SOUND 0x71
#define CMD_ACK 0xFA
#define CMD_VSYNC 0xFB

// Error codes
typedef enum {
    ERR_NONE = 0,
    ERR_TIMEOUT = 1,
    ERR_INVALID_COMMAND = 2
} ErrorCode;

#endif // TRIBOY_COMMON_H