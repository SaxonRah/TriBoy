#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "common.h"

// Connect the SDA and SCL pins to 6 and 7 on the Pico for i2c0

int main() {
    // Initialize USB serial
    stdio_init_all();
    
    // Configure I2C1 at 100 kHz
    i2c_init(GPU_DBG_I2C, 100 * 1000);

    // Configure I2C0 at 100 kHz
    // i2c_init(i2c0, 100 * 1000);
    
    // Set up the pins
    //
    //
    // gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    // gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_set_function(GPU_DBG_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(GPU_DBG_SCL_PIN, GPIO_FUNC_I2C);
    
    // Enable internal pullups
    // gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    // gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    gpio_pull_up(GPU_DBG_SDA_PIN);
    gpio_pull_up(GPU_DBG_SCL_PIN);
    
    // Wait for serial connection
    sleep_ms(2000);
    printf("I2C Scanner initialized\n");
    
    while (1) {
        printf("Scanning I2C bus...\n");
        
        int num_devices = 0;
        uint8_t address;
        
        // Scan addresses 1-127 (0 is reserved for general call)
        for (address = 1; address < 128; address++) {
            uint8_t rxdata;
            // int ret = i2c_read_blocking(GPU_DBG_I2C, address, &rxdata, 1, false);
            int ret = i2c_read_blocking(i2c0, address, &rxdata, 1, false);
            
            printf("Reading address 0x%02X\n", address);
            // Found a device
            if (ret >= 0) {
                printf("I2C device found at address 0x%02X\n", address);
                num_devices++;
            }
        }
        
        if (num_devices == 0) {
            printf("No I2C devices found\n");
        } else {
            printf("Scan complete. Found %d device(s)\n", num_devices);
        }
        
        printf("\n");
        sleep_ms(5000);
    }
    
    return 0;
}