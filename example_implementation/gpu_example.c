// gpu_main.c - TriBoy GPU implementation
// Simple yet expandable setup for implementing the TriBoy three-microcontroller architecture.
// Never compiled, just an example.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

// Pin definitions
#define CPU_SPI_PORT spi0
#define CPU_MISO_PIN 0
#define CPU_MOSI_PIN 1  
#define CPU_SCK_PIN 2
#define CPU_CS_PIN 3
#define VSYNC_PIN 15
#define DISPLAY_DC_PIN 16
#define DISPLAY_CS_PIN 17
#define DISPLAY_SCK_PIN 18
#define DISPLAY_MOSI_PIN 19
#define DISPLAY_RST_PIN 20

// Display configuration
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 240
#define DISPLAY_BPP 8
#define BYTES_PER_PIXEL (DISPLAY_BPP / 8)
#define FRAMEBUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT * BYTES_PER_PIXEL)

// Layer and sprite limits
#define MAX_LAYERS 4
#define MAX_SPRITES 64
#define MAX_DIRTY_REGIONS 16

// Frame synchronization
bool vblank_callback_enabled = false;
volatile bool frame_ready = false;

// Command buffer
#define CMD_BUFFER_SIZE 256
uint8_t cmd_buffer[CMD_BUFFER_SIZE];

// Framebuffer
uint8_t framebuffer[FRAMEBUFFER_SIZE];

// Basic layer structure
typedef struct {
    bool enabled;
    uint8_t priority;
    uint16_t scroll_x;
    uint16_t scroll_y;
    uint8_t* tilemap;
    uint8_t tile_width;
    uint8_t tile_height;
    uint8_t width_tiles;
    uint8_t height_tiles;
} Layer;

// Basic sprite structure
typedef struct {
    bool visible;
    uint8_t pattern_id;
    int16_t x;
    int16_t y;
    uint8_t attributes;
    uint8_t palette_offset;
    uint8_t scale;
} Sprite;

// Dirty rectangle for efficient updating
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} Rect;

// Global state
Layer layers[MAX_LAYERS];
Sprite sprites[MAX_SPRITES];
Rect dirty_regions[MAX_DIRTY_REGIONS];
int dirty_region_count = 0;

// Display SPI functions
void display_init() {
    // Initialize SPI for display
    spi_init(spi1, 40000000); // 40MHz
    gpio_set_function(DISPLAY_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(DISPLAY_MOSI_PIN, GPIO_FUNC_SPI);
    
    gpio_init(DISPLAY_CS_PIN);
    gpio_set_dir(DISPLAY_CS_PIN, GPIO_OUT);
    gpio_put(DISPLAY_CS_PIN, 1);
    
    gpio_init(DISPLAY_DC_PIN);
    gpio_set_dir(DISPLAY_DC_PIN, GPIO_OUT);
    gpio_put(DISPLAY_DC_PIN, 1);
    
    gpio_init(DISPLAY_RST_PIN);
    gpio_set_dir(DISPLAY_RST_PIN, GPIO_OUT);
    
    // Reset display
    gpio_put(DISPLAY_RST_PIN, 0);
    sleep_ms(100);
    gpio_put(DISPLAY_RST_PIN, 1);
    sleep_ms(100);
    
    // Display initialization would go here
    // This depends on your specific display hardware
    // For example, for ILI9341:
    display_command(0x01); // Software reset
    sleep_ms(100);
    
    display_command(0x11); // Sleep out
    sleep_ms(120);
    
    display_command(0x3A); // Set color mode
    display_data(0x05);    // 16-bit color
    
    display_command(0x29); // Display on
    
    printf("Display initialized\n");
}

void display_command(uint8_t cmd) {
    gpio_put(DISPLAY_DC_PIN, 0); // Command mode
    gpio_put(DISPLAY_CS_PIN, 0);
    spi_write_blocking(spi1, &cmd, 1);
    gpio_put(DISPLAY_CS_PIN, 1);
}

void display_data(uint8_t data) {
    gpio_put(DISPLAY_DC_PIN, 1); // Data mode
    gpio_put(DISPLAY_CS_PIN, 0);
    spi_write_blocking(spi1, &data, 1);
    gpio_put(DISPLAY_CS_PIN, 1);
}

void display_data_bulk(const uint8_t* data, size_t len) {
    gpio_put(DISPLAY_DC_PIN, 1); // Data mode
    gpio_put(DISPLAY_CS_PIN, 0);
    spi_write_blocking(spi1, data, len);
    gpio_put(DISPLAY_CS_PIN, 1);
}

void display_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint8_t data[4];
    
    display_command(0x2A); // Column address set
    data[0] = x >> 8;
    data[1] = x & 0xFF;
    data[2] = (x + w - 1) >> 8;
    data[3] = (x + w - 1) & 0xFF;
    display_data_bulk(data, 4);
    
    display_command(0x2B); // Row address set
    data[0] = y >> 8;
    data[1] = y & 0xFF;
    data[2] = (y + h - 1) >> 8;
    data[3] = (y + h - 1) & 0xFF;
    display_data_bulk(data, 4);
    
    display_command(0x2C); // Memory write
}

// CPU SPI communication
void init_cpu_spi() {
    // Set up SPI slave for communication with CPU
    spi_init(CPU_SPI_PORT, 20000000);
    spi_set_slave(CPU_SPI_PORT, true);
    
    gpio_set_function(CPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(CPU_CS_PIN);
    gpio_set_dir(CPU_CS_PIN, GPIO_IN);
    gpio_pull_up(CPU_CS_PIN);
    
    // Set up VSYNC output
    gpio_init(VSYNC_PIN);
    gpio_set_dir(VSYNC_PIN, GPIO_OUT);
    gpio_put(VSYNC_PIN, 1);
    
    printf("CPU SPI interface initialized\n");
}

// Drawing functions
void clear_framebuffer() {
    memset(framebuffer, 0, FRAMEBUFFER_SIZE);
}

void mark_rect_dirty(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    if (dirty_region_count < MAX_DIRTY_REGIONS) {
        Rect* r = &dirty_regions[dirty_region_count++];
        r->x = x;
        r->y = y;
        r->width = width;
        r->height = height;
    } else {
        // If too many regions, mark the whole screen dirty
        dirty_regions[0].x = 0;
        dirty_regions[0].y = 0;
        dirty_regions[0].width = DISPLAY_WIDTH;
        dirty_regions[0].height = DISPLAY_HEIGHT;
        dirty_region_count = 1;
    }
}

void reset_dirty_regions() {
    dirty_region_count = 0;
}

// Command processing
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    switch (cmd_id) {
        case 0x00: // NOP
            break;
            
        case 0x01: // RESET_GPU
            for (int i = 0; i < MAX_LAYERS; i++) {
                layers[i].enabled = false;
            }
            
            for (int i = 0; i < MAX_SPRITES; i++) {
                sprites[i].visible = false;
            }
            
            clear_framebuffer();
            mark_rect_dirty(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            break;
            
        case 0x02: // SET_DISPLAY_MODE
            // Not fully implemented in this basic version
            break;
            
        case 0x03: // SET_VBLANK_CALLBACK
            vblank_callback_enabled = data[0] != 0;
            break;
            
        case 0x20: // CONFIGURE_LAYER
            {
                uint8_t layer_id = data[0];
                if (layer_id < MAX_LAYERS) {
                    layers[layer_id].enabled = data[1] != 0;
                    layers[layer_id].priority = data[2];
                    layers[layer_id].tile_width = data[5];
                    layers[layer_id].tile_height = data[6];
                    layers[layer_id].width_tiles = data[7];
                    layers[layer_id].height_tiles = data[8];
                    
                    mark_rect_dirty(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                }
            }
            break;
            
        case 0x23: // SCROLL_LAYER
            {
                uint8_t layer_id = data[0];
                if (layer_id < MAX_LAYERS) {
                    layers[layer_id].scroll_x = (data[1] << 8) | data[2];
                    layers[layer_id].scroll_y = (data[3] << 8) | data[4];
                    
                    mark_rect_dirty(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                }
            }
            break;
            
        case 0x41: // DEFINE_SPRITE
            {
                uint8_t sprite_id = data[0];
                if (sprite_id < MAX_SPRITES) {
                    sprites[sprite_id].pattern_id = data[1];
                    sprites[sprite_id].x = (data[2] << 8) | data[3];
                    sprites[sprite_id].y = (data[4] << 8) | data[5];
                    sprites[sprite_id].attributes = data[6];
                    sprites[sprite_id].palette_offset = data[7];
                    sprites[sprite_id].scale = data[8];
                    sprites[sprite_id].visible = true;
                    
                    // Mark sprite area as dirty
                    mark_rect_dirty(sprites[sprite_id].x, sprites[sprite_id].y, 16, 16);
                }
            }
            break;
            
        case 0x42: // MOVE_SPRITE
            {
                uint8_t sprite_id = data[0];
                if (sprite_id < MAX_SPRITES && sprites[sprite_id].visible) {
                    // Mark old position as dirty
                    mark_rect_dirty(sprites[sprite_id].x, sprites[sprite_id].y, 16, 16);
                    
                    // Update position
                    sprites[sprite_id].x = (data[1] << 8) | data[2];
                    sprites[sprite_id].y = (data[3] << 8) | data[4];
                    
                    // Mark new position as dirty
                    mark_rect_dirty(sprites[sprite_id].x, sprites[sprite_id].y, 16, 16);
                }
            }
            break;
            
        // Add more commands as needed
    }
}

// Rendering functions (simplified)
void render_sprite(uint8_t sprite_id) {
    Sprite* sprite = &sprites[sprite_id];
    if (!sprite->visible) return;
    
    // In a real implementation, this would draw the sprite
    // This is a placeholder that just draws a simple box
    int x = sprite->x;
    int y = sprite->y;
    uint8_t color = sprite->pattern_id + 1; // Use pattern ID as color for demo
    
    for (int py = 0; py < 16; py++) {
        for (int px = 0; px < 16; px++) {
            int pos_x = x + px;
            int pos_y = y + py;
            
            if (pos_x >= 0 && pos_x < DISPLAY_WIDTH && pos_y >= 0 && pos_y < DISPLAY_HEIGHT) {
                framebuffer[pos_y * DISPLAY_WIDTH + pos_x] = color;
            }
        }
    }
}

void render_layer(uint8_t layer_id) {
    Layer* layer = &layers[layer_id];
    if (!layer->enabled) return;
    
    // In a real implementation, this would render the tile layer
    // This is a placeholder that just draws a simple pattern
    uint8_t color = layer_id + 1;
    
    for (int y = 0; y < DISPLAY_HEIGHT; y += 32) {
        for (int x = 0; x < DISPLAY_WIDTH; x += 32) {
            // Draw a checker pattern
            if (((x + layer->scroll_x) / 32 + (y + layer->scroll_y) / 32) % 2 == 0) {
                for (int py = 0; py < 32; py++) {
                    for (int px = 0; px < 32; px++) {
                        int pos_x = x + px;
                        int pos_y = y + py;
                        
                        if (pos_x >= 0 && pos_x < DISPLAY_WIDTH && pos_y >= 0 && pos_y < DISPLAY_HEIGHT) {
                            framebuffer[pos_y * DISPLAY_WIDTH + pos_x] = color;
                        }
                    }
                }
            }
        }
    }
}

void render_frame() {
    // Clear framebuffer
    clear_framebuffer();
    
    // Render layers by priority
    for (int p = 3; p >= 0; p--) {
        for (int l = 0; l < MAX_LAYERS; l++) {
            if (layers[l].enabled && layers[l].priority == p) {
                render_layer(l);
            }
        }
    }
    
    // Render sprites
    for (int s = 0; s < MAX_SPRITES; s++) {
        render_sprite(s);
    }
    
    // Update display with framebuffer content
    display_set_window(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
    // Convert 8bpp to 16bpp for display
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i += 160) { // Process in chunks
        uint16_t display_data[160];
        for (int j = 0; j < 160 && (i + j) < DISPLAY_WIDTH * DISPLAY_HEIGHT; j++) {
            // Simple color mapping for demo (in real implementation, use a palette)
            switch (framebuffer[i + j]) {
                case 0: display_data[j] = 0x0000; break; // Black
                case 1: display_data[j] = 0xF800; break; // Red
                case 2: display_data[j] = 0x07E0; break; // Green
                case 3: display_data[j] = 0x001F; break; // Blue
                case 4: display_data[j] = 0xFFE0; break; // Yellow
                default: display_data[j] = 0xFFFF; break; // White
            }
        }
        
        gpio_put(DISPLAY_DC_PIN, 1); // Data mode
        gpio_put(DISPLAY_CS_PIN, 0);
        spi_write_blocking(spi1, (uint8_t*)display_data, 160 * 2);
        gpio_put(DISPLAY_CS_PIN, 1);
    }
    
    // Reset dirty regions after rendering
    reset_dirty_regions();
}

// Core 1 rendering function
void core1_entry() {
    printf("GPU Core 1 started\n");
    
    while (true) {
        // Wait for frame ready signal
        while (!frame_ready) {
            tight_loop_contents();
        }
        
        // Render the frame
        render_frame();
        
        // Signal VSYNC to CPU
        if (vblank_callback_enabled) {
            gpio_put(VSYNC_PIN, 0);
            sleep_us(100);
            gpio_put(VSYNC_PIN, 1);
        }
        
        // Clear frame ready flag
        frame_ready = false;
    }
}

// Main function
int main() {
    stdio_init_all();
    printf("TriBoy GPU initializing...\n");
    
    // Initialize hardware
    init_cpu_spi();
    display_init();
    
    // Initialize state
    for (int i = 0; i < MAX_LAYERS; i++) {
        layers[i].enabled = false;
    }
    
    for (int i = 0; i < MAX_SPRITES; i++) {
        sprites[i].visible = false;
    }
    
    clear_framebuffer();
    
    // Start Core 1 for rendering
    multicore_launch_core1(core1_entry);
    
    // Set up timer for consistent frame rate
    uint32_t last_frame_time = time_us_32();
    uint32_t frame_target = 16667; // ~60fps
    
    // Main loop
    while (true) {
        // Check for commands from CPU
        if (!gpio_get(CPU_CS_PIN)) {
            // CS asserted, read command
            uint8_t cmd_id, length;
            
            spi_read_blocking(CPU_SPI_PORT, 0, &cmd_id, 1);
            spi_read_blocking(CPU_SPI_PORT, 0, &length, 1);
            
            // Read remaining data
            if (length > 2) {
                spi_read_blocking(CPU_SPI_PORT, 0, cmd_buffer, length - 2);
            }
            
            // Process command
            process_command(cmd_id, cmd_buffer, length - 2);
        }
        
        // Check if it's time for a new frame
        uint32_t current_time = time_us_32();
        if (current_time - last_frame_time >= frame_target) {
            // Signal Core 1 to render a new frame
            frame_ready = true;
            last_frame_time = current_time;
        }
    }
    
    return 0;
}
