// cpu_main.c - TriBoy CPU implementation
// Simple yet expandable setup for implementing the TriBoy three-microcontroller architecture.
// Never compiled, just an example.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/sync.h"

// Pin definitions
#define GPU_SPI_PORT spi0
#define APU_SPI_PORT spi1
#define GPU_CS_PIN 5
#define APU_CS_PIN 13
#define GPU_SCK_PIN 2
#define GPU_MOSI_PIN 3
#define GPU_MISO_PIN 4
#define APU_SCK_PIN 10
#define APU_MOSI_PIN 11
#define APU_MISO_PIN 12
#define VSYNC_PIN 15   // From GPU
#define SD_CS_PIN 22
#define SD_SCK_PIN 18
#define SD_MOSI_PIN 19
#define SD_MISO_PIN 20

// Command queue definitions
#define GPU_QUEUE_SIZE 64
#define APU_QUEUE_SIZE 64

// Message types between cores
typedef enum {
    MSG_LOAD_ASSET,
    MSG_PROCESS_GPU_QUEUE,
    MSG_PROCESS_APU_QUEUE,
    MSG_GAME_EVENT
} MessageType;

typedef struct {
    MessageType type;
    uint32_t param1;
    uint32_t param2;
    void* data;
} CoreMessage;

// Command structure
typedef struct {
    uint8_t command_id;
    uint8_t length;
    uint8_t data[256];
} Command;

// Command queues
typedef struct {
    Command commands[GPU_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    mutex_t lock;
} CommandQueue;

// Global variables
CommandQueue gpu_queue;
CommandQueue apu_queue;
bool vsync_received = false;
uint32_t frame_counter = 0;

// Initialize hardware
void init_hardware() {
    stdio_init_all();
    printf("TriBoy CPU initializing...\n");
    
    // Initialize SPI for GPU communication
    spi_init(GPU_SPI_PORT, 20000000); // 20MHz
    gpio_set_function(GPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(GPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(GPU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(GPU_CS_PIN);
    gpio_set_dir(GPU_CS_PIN, GPIO_OUT);
    gpio_put(GPU_CS_PIN, 1);
    
    // Initialize SPI for APU communication
    spi_init(APU_SPI_PORT, 20000000); // 20MHz
    gpio_set_function(APU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(APU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(APU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(APU_CS_PIN);
    gpio_set_dir(APU_CS_PIN, GPIO_OUT);
    gpio_put(APU_CS_PIN, 1);
    
    // Initialize VSYNC interrupt
    gpio_init(VSYNC_PIN);
    gpio_set_dir(VSYNC_PIN, GPIO_IN);
    gpio_pull_up(VSYNC_PIN);
    
    // Initialize command queues
    mutex_init(&gpu_queue.lock);
    mutex_init(&apu_queue.lock);
    gpu_queue.head = 0;
    gpu_queue.tail = 0;
    gpu_queue.count = 0;
    apu_queue.head = 0;
    apu_queue.tail = 0;
    apu_queue.count = 0;
}

// GPU command queue functions
bool queue_gpu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    mutex_enter_blocking(&gpu_queue.lock);
    
    if (gpu_queue.count >= GPU_QUEUE_SIZE) {
        mutex_exit(&gpu_queue.lock);
        return false;
    }
    
    Command* cmd = &gpu_queue.commands[gpu_queue.tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    memcpy(cmd->data, data, length - 2);
    
    gpu_queue.tail = (gpu_queue.tail + 1) % GPU_QUEUE_SIZE;
    gpu_queue.count++;
    
    mutex_exit(&gpu_queue.lock);
    return true;
}

// APU command queue functions
bool queue_apu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    mutex_enter_blocking(&apu_queue.lock);
    
    if (apu_queue.count >= APU_QUEUE_SIZE) {
        mutex_exit(&apu_queue.lock);
        return false;
    }
    
    Command* cmd = &apu_queue.commands[apu_queue.tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    memcpy(cmd->data, data, length - 2);
    
    apu_queue.tail = (apu_queue.tail + 1) % APU_QUEUE_SIZE;
    apu_queue.count++;
    
    mutex_exit(&apu_queue.lock);
    return true;
}

// Process GPU commands
void process_gpu_queue() {
    const int MAX_PROCESS = 10;
    int processed = 0;
    
    while (processed < MAX_PROCESS) {
        mutex_enter_blocking(&gpu_queue.lock);
        
        if (gpu_queue.count == 0) {
            mutex_exit(&gpu_queue.lock);
            break;
        }
        
        Command* cmd = &gpu_queue.commands[gpu_queue.head];
        
        uint8_t buffer[258];
        buffer[0] = cmd->command_id;
        buffer[1] = cmd->length;
        memcpy(&buffer[2], cmd->data, cmd->length - 2);
        
        gpu_queue.head = (gpu_queue.head + 1) % GPU_QUEUE_SIZE;
        gpu_queue.count--;
        mutex_exit(&gpu_queue.lock);
        
        gpio_put(GPU_CS_PIN, 0);
        spi_write_blocking(GPU_SPI_PORT, buffer, cmd->length);
        gpio_put(GPU_CS_PIN, 1);
        
        processed++;
        
        // Wait briefly before next command to ensure GPU can process
        sleep_us(10);
    }
}

// Process APU commands
void process_apu_queue() {
    const int MAX_PROCESS = 10;
    int processed = 0;
    
    while (processed < MAX_PROCESS) {
        mutex_enter_blocking(&apu_queue.lock);
        
        if (apu_queue.count == 0) {
            mutex_exit(&apu_queue.lock);
            break;
        }
        
        Command* cmd = &apu_queue.commands[apu_queue.head];
        
        uint8_t buffer[258];
        buffer[0] = cmd->command_id;
        buffer[1] = cmd->length;
        memcpy(&buffer[2], cmd->data, cmd->length - 2);
        
        apu_queue.head = (apu_queue.head + 1) % APU_QUEUE_SIZE;
        apu_queue.count--;
        mutex_exit(&apu_queue.lock);
        
        gpio_put(APU_CS_PIN, 0);
        spi_write_blocking(APU_SPI_PORT, buffer, cmd->length);
        gpio_put(APU_CS_PIN, 1);
        
        processed++;
        
        // Wait briefly before next command to ensure APU can process
        sleep_us(10);
    }
}

// Initialize GPU
void init_gpu() {
    // Reset GPU
    uint8_t reset_cmd[2] = {0x01, 2}; // RESET_GPU command
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(GPU_SPI_PORT, reset_cmd, 2);
    gpio_put(GPU_CS_PIN, 1);
    sleep_ms(100); // Wait for GPU to reset
    
    // Set display mode (320x240x8)
    uint8_t display_cmd[7] = {0x02, 7, 0x01, 0x40, 0x00, 0xF0, 8}; // SET_DISPLAY_MODE
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(GPU_SPI_PORT, display_cmd, 7);
    gpio_put(GPU_CS_PIN, 1);
    sleep_ms(100);
    
    // Enable VSYNC callback
    uint8_t vsync_cmd[3] = {0x03, 3, 1}; // SET_VBLANK_CALLBACK
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(GPU_SPI_PORT, vsync_cmd, 3);
    gpio_put(GPU_CS_PIN, 1);
    
    printf("GPU Initialized\n");
}

// Initialize APU
void init_apu() {
    // Reset APU
    uint8_t reset_cmd[2] = {0x01, 2}; // RESET_AUDIO command
    gpio_put(APU_CS_PIN, 0);
    spi_write_blocking(APU_SPI_PORT, reset_cmd, 2);
    gpio_put(APU_CS_PIN, 1);
    sleep_ms(100); // Wait for APU to reset
    
    // Set master volume
    uint8_t volume_cmd[3] = {0x02, 3, 200}; // SET_MASTER_VOLUME
    gpio_put(APU_CS_PIN, 0);
    spi_write_blocking(APU_SPI_PORT, volume_cmd, 3);
    gpio_put(APU_CS_PIN, 1);
    
    printf("APU Initialized\n");
}

// Simple game state
typedef struct {
    int16_t player_x;
    int16_t player_y;
    uint8_t player_sprite_id;
    uint8_t score;
    bool game_active;
} GameState;

GameState game;

// High-level game API functions
void init_game() {
    game.player_x = 160;
    game.player_y = 120;
    game.player_sprite_id = 0;
    game.score = 0;
    game.game_active = true;
    
    // Load background layer
    uint8_t layer_cmd[10] = {0x20, 10, 0, 1, 0, 0, 8, 8, 40, 30}; // CONFIGURE_LAYER
    queue_gpu_command(0x20, 10, layer_cmd);
    
    // Define player sprite
    uint8_t sprite_cmd[10] = {0x41, 10, 0, 0, 0, 160, 0, 120, 0, 0, 128}; // DEFINE_SPRITE
    queue_gpu_command(0x41, 10, sprite_cmd);
    
    // Play background music
    uint8_t music_cmd[2] = {0, 0}; // TRACKER_PLAY
    queue_apu_command(0x11, 2, music_cmd);
}

// Update game state each frame
void update_game() {
    // Simple player movement logic (would normally use input)
    game.player_x += 1;
    if (game.player_x > 300) game.player_x = 20;
    
    // Update sprite position
    uint8_t move_cmd[6] = {0x42, 6, 0, (uint8_t)(game.player_x >> 8), (uint8_t)game.player_x, 
                           (uint8_t)(game.player_y >> 8), (uint8_t)game.player_y};
    queue_gpu_command(0x42, 6, move_cmd);
    
    // Play a sound every 30 frames
    if (frame_counter % 30 == 0) {
        uint8_t sound_cmd[5] = {0x71, 5, 0, 1, 64, 200}; // SAMPLE_PLAY
        queue_apu_command(0x71, 5, sound_cmd);
    }
    
    frame_counter++;
}

// Core 1 entry point
void core1_entry() {
    printf("CPU Core 1 started\n");
    
    while (true) {
        // Process GPU queue when it has data
        if (gpu_queue.count > 0) {
            process_gpu_queue();
        }
        
        // Process APU queue when it has data
        if (apu_queue.count > 0) {
            process_apu_queue();
        }
        
        // Check for VSYNC
        if (gpio_get(VSYNC_PIN) == 0) {
            vsync_received = true;
        }
        
        // Yield to allow Core 0 processing time
        sleep_us(100);
    }
}

// Main entry point
int main() {
    // Initialize hardware
    init_hardware();
    
    // Start Core 1
    multicore_launch_core1(core1_entry);
    
    // Initialize peripherals
    init_gpu();
    init_apu();
    
    // Initialize game
    init_game();
    
    // Main game loop
    while (true) {
        uint32_t frame_start = time_us_32();
        
        // Wait for VSYNC to synchronize with display
        vsync_received = false;
        while (!vsync_received) {
            tight_loop_contents();
        }
        
        // Process game logic
        update_game();
        
        // Calculate frame time
        uint32_t frame_time = time_us_32() - frame_start;
        
        // Target 60 FPS (16.67ms)
        uint32_t target_frame_time = 16667;
        if (frame_time < target_frame_time) {
            sleep_us(target_frame_time - frame_time);
        }
    }
    
    return 0;
}
