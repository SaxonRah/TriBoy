// triboy_common.h
#ifndef TRIBOY_COMMON_H
#define TRIBOY_COMMON_H

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

// SPI configuration
#define SPI_FREQUENCY 8000000  // 8MHz
// SPI0 bus is shared between CPU and GPU
#define SPI0_BUS spi0
// SPI1 bus is used for CPU to APU communication
#define SPI1_BUS spi1

#define CPU_DBG_I2C i2c0
#define GPU_DBG_I2C i2c0
#define APU_DBG_I2C i2c0

// Device-specific ports (for clarity)
#define CPU_GPU_SPI_PORT SPI0_BUS
#define GPU_SPI_PORT SPI0_BUS
#define CPU_APU_SPI_PORT SPI1_BUS
#define APU_SPI_PORT SPI1_BUS

// PIN definitions
// SPI0 for CPU to GPU communication
#define CPU_GPU_SCK_PIN 4
#define CPU_TX_PIN 5      // CPU output to GPU
#define CPU_RX_PIN 6      // CPU input from GPU
#define GPU_RX_PIN 6      // GPU input from CPU (connects to CPU_TX_PIN)
#define GPU_TX_PIN 5      // GPU output to CPU (connects to CPU_RX_PIN)
#define GPU_CS_PIN 7
#define GPU_DATA_READY_PIN 9
#define GPU_VSYNC_PIN 10

// SPI1 for CPU to APU communication
#define CPU_APU_SCK_PIN 14
#define CPU_APU_TX_PIN 15  // CPU output to APU
#define CPU_APU_RX_PIN 16  // CPU input from APU
#define APU_RX_PIN 16      // APU input from CPU (connects to CPU_APU_TX_PIN)
#define APU_TX_PIN 15      // APU output to CPU (connects to CPU_APU_RX_PIN)
#define APU_CS_PIN 17
#define APU_DATA_READY_PIN 19

// I2C Debug Screens: These I2C screens connect directly to each sub-Pico Module (e.g. CPU, GPU, APU each have a 16x2 Character Screen)
// THESE SHOULD BE GPIO NUMBERS NOT PHYSICAL PIN NUMBERS
// #define CPU_DBG_VBUS_PIN 40
// #define CPU_DBG_GND_PIN 38
// #define GPU_DBG_VBUS_PIN 40
// #define GPU_DBG_GND_PIN 38
// #define APU_DBG_VBUS_PIN 40
// #define APU_DBG_GND_PIN 38
#define CPU_DBG_SCL_PIN 21
#define CPU_DBG_SDA_PIN 20
#define GPU_DBG_SCL_PIN 21 // i2c0
#define GPU_DBG_SDA_PIN 20 // i2c0
// #define GPU_DBG_SCL_PIN PICO_DEFAULT_I2C_SCL_PIN
// #define GPU_DBG_SDA_PIN PICO_DEFAULT_I2C_SDA_PIN
#define APU_DBG_SCL_PIN 21
#define APU_DBG_SDA_PIN 20


// Command IDs
#define CMD_NOP 0x00
#define CMD_RESET_GPU 0x01
#define CMD_RESET_AUDIO 0x01
#define CMD_SET_VSYNC_CALLBACK 0x03
#define CMD_VSYNC_WAIT 0x04
#define CMD_ENABLE_SPI_VSYNC 0x05
#define CMD_DISABLE_SPI_VSYNC 0x06
#define CMD_PLAY_SOUND 0x71
#define CMD_ACK 0xFA
#define CMD_VSYNC 0xFB


// Error codes
typedef enum {
    ERR_NONE = 0,
    ERR_TIMEOUT = 1,
    ERR_INVALID_COMMAND = 2,
    ERR_INVALID_PARAMS = 3,
    ERR_BUSY = 4
} ErrorCode;

#endif // TRIBOY_COMMON_H
