# Implementing PIO-Based Hardware Scrolling Acceleration for TriBoy GPU
Scrolling is one of the most performance-intensive operations in 2D games. By leveraging the RP2040/RP2350's Programmable I/O (PIO) blocks, we can significantly optimize full-screen scrolling operations without taxing the CPU cores.

## PIO Programs for Tile Shifting
PIO programs that will handle the hardware-accelerated tile shifting:

```c
// Add to gpu.c (or create gpu_pio_programs.c)

// PIO program for horizontal scrolling acceleration
const uint16_t horizontal_scroll_program_instructions[] = {
    // Read source address into OSR
    0x80a0, //  0: pull block              
    0xa027, //  1: mov x, osr              
    // Read destination address into Y
    0x80a0, //  2: pull block              
    0xa047, //  3: mov y, osr              
    // Read tile count into ISR
    0x80a0, //  4: pull block              
    0xa0e7, //  5: mov isr, osr            
    // Main copy loop
    0xb0c2, //  6: mov osr, isr            
    0x6003, //  7: out x, 3                ; Decrement count by 1
    0x0067, //  8: jmp !x 7f               ; Exit when done
    // Perform memory copy
    0xb042, //  9: mov osr, x              ; Load source address
    0x8020, // 10: push block              ; Push read request
    0x60c0, // 11: out pins, 32            ; Read data to pins (32 bits)
    0xb0c2, // 12: mov osr, isr            ; Load data from pins to OSR
    0xb082, // 13: mov osr, y              ; Load destination address
    0x8020, // 14: push block              ; Push write request
    0xa0c2, // 15: mov isr, osr            ; Save write data to ISR
    0xa021, // 16: mov x, pins             ; Increment source address
    0xa042, // 17: mov y, x                ; Increment destination address
    0x0046, // 18: jmp 6b                  ; Loop back for next tile
    0x0000, // 19: jmp 0                   ; Should never reach here
    // Exit
    0xa0c2, // 20: mov isr, osr            ; 7: Signal completion
    0x8020, // 21: push block
};

// PIO program for vertical scrolling acceleration
const uint16_t vertical_scroll_program_instructions[] = {
    // Similar structure but adapted for vertical movement
    // ...
};

// Define the PIO state machines
struct {
    PIO pio;
    uint sm;
    uint offset;
} h_scroll_sm, v_scroll_sm;
```

## Initialization Functions
Create initialization functions that load the PIO programs:

```c
// Initialize horizontal scrolling PIO
void init_horizontal_scroll_pio() {
    // Choose which PIO block to use (0 or 1)
    h_scroll_sm.pio = pio0;
    
    // Find a free state machine in the PIO
    h_scroll_sm.sm = pio_claim_unused_sm(h_scroll_sm.pio, true);
    
    // Load the program into the PIO's instruction memory
    h_scroll_sm.offset = pio_add_program(h_scroll_sm.pio, 
                                        horizontal_scroll_program_instructions);
    
    // Configure the state machine
    pio_sm_config c = pio_get_default_sm_config();
    
    // Configure clock divider (determine speed of execution)
    sm_config_set_clkdiv(&c, 2.0); // Run at system clock / 2
    
    // Configure pin directions and mapping
    sm_config_set_out_pins(&c, PIN_DATA_START, 8); // 8 data pins
    sm_config_set_in_pins(&c, PIN_DATA_START);
    sm_config_set_out_shift(&c, false, true, 32);  // 32-bit output shift right
    sm_config_set_in_shift(&c, false, true, 32);   // 32-bit input shift right
    
    // Set pins as outputs for this state machine
    pio_sm_set_consecutive_pindirs(h_scroll_sm.pio, h_scroll_sm.sm, 
                                  PIN_DATA_START, 8, true);
    
    // Load configuration and initialize state machine
    pio_sm_init(h_scroll_sm.pio, h_scroll_sm.sm, h_scroll_sm.offset, &c);
    
    // Start the state machine
    pio_sm_set_enabled(h_scroll_sm.pio, h_scroll_sm.sm, true);
}

// Initialize vertical scrolling PIO
void init_vertical_scroll_pio() {
    // Similar to horizontal but using the vertical program
    // ...
}
```

## Hardware-Accelerated Scrolling Functions
With the PIO programs loaded, the main scrolling acceleration functions:

```c
// Perform hardware-accelerated horizontal scrolling
void hw_scroll_horizontal(uint8_t layer_id, int16_t scroll_amount) {
    Layer* layer = &layers[layer_id];
    
    // Only use hardware acceleration for full screen layers
    if (!layer->enabled || layer->width_tiles * layer->tile_width < display_width) {
        // Fall back to software scrolling
        scroll_layer(layer_id, scroll_amount, layer->scroll_y);
        return;
    }
    
    // Calculate the new scroll position
    uint16_t new_scroll_x = (layer->scroll_x + scroll_amount) % 
                          (layer->width_tiles * layer->tile_width);
    
    // Determine which rows of tiles need updating
    uint16_t old_tile_column = layer->scroll_x / layer->tile_width;
    uint16_t new_tile_column = new_scroll_x / layer->tile_width;
    
    // If tile column hasn't changed, just update the scroll value
    if (old_tile_column == new_tile_column) {
        layer->scroll_x = new_scroll_x;
        return;
    }
    
    // Calculate how many tile columns need to be updated
    int tile_columns_to_update = abs(new_tile_column - old_tile_column);
    
    // For each row of tiles on screen
    for (int row = 0; row < display_height / layer->tile_height; row++) {
        // Calculate the source and destination addresses for this row
        uint32_t row_start_addr = (uint32_t)framebuffer + 
                                (row * layer->tile_height * display_width);
        
        // Use the PIO to shift this row of tiles
        hw_shift_row(row_start_addr, scroll_amount, display_width);
        
        // Now draw the new tiles that just scrolled into view
        for (int i = 0; i < tile_columns_to_update; i++) {
            int tile_col;
            if (scroll_amount > 0) {
                // Scrolling right, draw new tiles on the right edge
                tile_col = (new_tile_column + display_width / layer->tile_width - i - 1) % 
                         layer->width_tiles;
            } else {
                // Scrolling left, draw new tiles on the left edge
                tile_col = (new_tile_column + i) % layer->width_tiles;
            }
            
            // Draw the tile at (tile_col, row)
            TileInfo* tile = &layer->tilemap[row * layer->width_tiles + tile_col];
            uint8_t* tile_data = get_cached_tile(layer_id, tile->tile_id);
            
            if (tile_data != NULL) {
                // Calculate screen position
                int screen_x;
                if (scroll_amount > 0) {
                    // Right edge
                    screen_x = display_width - (i + 1) * layer->tile_width;
                } else {
                    // Left edge
                    screen_x = i * layer->tile_width;
                }
                
                int screen_y = row * layer->tile_height;
                
                // Draw the tile
                render_tile(screen_x, screen_y, tile_data, tile->attributes,
                          layer->tile_width, layer->tile_height, layer->bpp);
            }
        }
    }
    
    // Update the layer's scroll position
    layer->scroll_x = new_scroll_x;
}

// Hardware-accelerated row shifting using PIO
void hw_shift_row(uint32_t row_address, int16_t shift_amount, uint16_t width) {
    // Make sure PIO state machine is ready
    while (pio_sm_is_tx_fifo_full(h_scroll_sm.pio, h_scroll_sm.sm)) {
        tight_loop_contents();
    }
    
    // Calculate source and destination addresses for copying
    uint32_t src_addr, dst_addr;
    uint16_t copy_width;
    
    if (shift_amount > 0) {
        // Shifting right - move data from right to left
        src_addr = row_address;
        dst_addr = row_address + shift_amount;
        copy_width = width - shift_amount;
    } else {
        // Shifting left - move data from left to right
        shift_amount = -shift_amount; // Make positive
        src_addr = row_address + shift_amount;
        dst_addr = row_address;
        copy_width = width - shift_amount;
    }
    
    // Ensure we're copying full 32-bit words
    uint32_t words_to_copy = (copy_width + 3) / 4;
    
    // Configure the PIO operation
    pio_sm_put_blocking(h_scroll_sm.pio, h_scroll_sm.sm, src_addr);  // Source
    pio_sm_put_blocking(h_scroll_sm.pio, h_scroll_sm.sm, dst_addr);  // Destination
    pio_sm_put_blocking(h_scroll_sm.pio, h_scroll_sm.sm, words_to_copy); // Count
    
    // Wait for completion
    while (pio_sm_get_rx_fifo_level(h_scroll_sm.pio, h_scroll_sm.sm) == 0) {
        tight_loop_contents();
    }
    
    // Read completion value
    pio_sm_get(h_scroll_sm.pio, h_scroll_sm.sm);
}
```

## Hardware-Accelerated Tile Blitting
In addition to scrolling, we can accelerate the process of copying tiles from the cache to the framebuffer:

```c
// PIO program for fast tile blitting
const uint16_t tile_blit_program_instructions[] = {
    // Simplified PIO instructions for copying a tile to framebuffer
    // ...
};

struct {
    PIO pio;
    uint sm;
    uint offset;
} tile_blit_sm;

// Initialize tile blitting PIO
void init_tile_blit_pio() {
    // Similar initialization as for scrolling
    // ...
}

// Hardware-accelerated tile blitting
void hw_blit_tile(uint8_t* tile_data, uint32_t framebuffer_addr, 
                 uint8_t tile_width, uint8_t tile_height, uint8_t bpp) {
    // Skip if PIO is not available
    if (tile_blit_sm.pio == NULL) return;
    
    // Make sure PIO state machine is ready
    while (pio_sm_is_tx_fifo_full(tile_blit_sm.pio, tile_blit_sm.sm)) {
        tight_loop_contents();
    }
    
    // Calculate bytes per tile and words per tile
    uint32_t bytes_per_tile = (tile_width * tile_height * bpp) / 8;
    uint32_t words_per_tile = (bytes_per_tile + 3) / 4;
    
    // Configure the PIO operation
    pio_sm_put_blocking(tile_blit_sm.pio, tile_blit_sm.sm, (uint32_t)tile_data);
    pio_sm_put_blocking(tile_blit_sm.pio, tile_blit_sm.sm, framebuffer_addr);
    pio_sm_put_blocking(tile_blit_sm.pio, tile_blit_sm.sm, words_per_tile);
    
    // Wait for completion
    while (pio_sm_get_rx_fifo_level(tile_blit_sm.pio, tile_blit_sm.sm) == 0) {
        tight_loop_contents();
    }
    
    // Read completion value
    pio_sm_get(tile_blit_sm.pio, tile_blit_sm.sm);
}
```

## Modified Render Function
Modify the tile rendering function to use our hardware acceleration:

```c
void render_tile(int x, int y, uint8_t* tile_data, uint8_t attributes,
                uint8_t tile_width, uint8_t tile_height, uint8_t bpp) {
    // Skip if fully outside the screen
    if (x >= display_width || y >= display_height || 
        x + tile_width <= 0 || y + tile_height <= 0) {
        return;
    }
    
    // Check if tile is unflipped and fully on screen
    bool can_use_hw_blit = (x >= 0 && y >= 0 && 
                          x + tile_width <= display_width && 
                          y + tile_height <= display_height &&
                          !(attributes & (ATTR_FLIP_X | ATTR_FLIP_Y)));
    
    if (can_use_hw_blit) {
        // Calculate framebuffer address for this tile
        uint32_t fb_addr = (uint32_t)framebuffer + (y * display_width + x);
        
        // Use hardware acceleration
        hw_blit_tile(tile_data, fb_addr, tile_width, tile_height, bpp);
    } else {
        // Fall back to software rendering for complex cases
        render_tile_software(x, y, tile_data, attributes, tile_width, tile_height, bpp);
    }
}
```

## Enhanced DMA Support for Scrolling
For even better performance, we can combine PIO with DMA for memory operations:

```c
// DMA channels for scrolling operations
int h_scroll_dma_channel;
int v_scroll_dma_channel;

// Initialize DMA for scrolling operations
void init_scroll_dma() {
    // Claim DMA channels
    h_scroll_dma_channel = dma_claim_unused_channel(true);
    v_scroll_dma_channel = dma_claim_unused_channel(true);
    
    // Configure horizontal scroll DMA channel
    dma_channel_config h_config = dma_channel_get_default_config(h_scroll_dma_channel);
    channel_config_set_transfer_data_size(&h_config, DMA_SIZE_32);
    channel_config_set_read_increment(&h_config, true);
    channel_config_set_write_increment(&h_config, true);
    
    // Configure vertical scroll DMA channel
    dma_channel_config v_config = dma_channel_get_default_config(v_scroll_dma_channel);
    channel_config_set_transfer_data_size(&v_config, DMA_SIZE_32);
    channel_config_set_read_increment(&v_config, true);
    channel_config_set_write_increment(&v_config, true);
    
    // Configure the channels but don't start them yet
    dma_channel_configure(
        h_scroll_dma_channel,
        &h_config,
        NULL,           // Write address (set for each operation)
        NULL,           // Read address (set for each operation)
        0,              // Transfer count (set for each operation)
        false           // Don't start yet
    );
    
    dma_channel_configure(
        v_scroll_dma_channel,
        &v_config,
        NULL,           // Write address (set for each operation)
        NULL,           // Read address (set for each operation)
        0,              // Transfer count (set for each operation)
        false           // Don't start yet
    );
}

// DMA-assisted horizontal scrolling for an entire row
void dma_scroll_row(uint32_t row_addr, int16_t shift_amount, uint16_t width) {
    uint32_t* src_addr;
    uint32_t* dst_addr;
    uint32_t words_to_copy;
    
    if (shift_amount > 0) {
        // Shifting right
        src_addr = (uint32_t*)row_addr;
        dst_addr = (uint32_t*)(row_addr + shift_amount);
        words_to_copy = (width - shift_amount + 3) / 4;
    } else {
        // Shifting left
        shift_amount = -shift_amount;
        src_addr = (uint32_t*)(row_addr + shift_amount);
        dst_addr = (uint32_t*)row_addr;
        words_to_copy = (width - shift_amount + 3) / 4;
    }
    
    // Configure and start DMA transfer
    dma_channel_transfer_from_buffer_now(h_scroll_dma_channel, 
                                        src_addr, words_to_copy);
    
    // Wait for DMA to complete
    dma_channel_wait_for_finish_blocking(h_scroll_dma_channel);
}
```

## RP2350-Specific Enhancements
For the RP2350 microcontroller with its expanded capabilities:

```c
#ifdef RP2350
// Enhanced parallel scrolling with multiple DMA channels
int parallel_dma_channels[4]; // Use multiple DMA channels for parallel operations

// Initialize parallel DMA for RP2350
void init_parallel_scroll_dma() {
    // Claim multiple DMA channels for parallel operations
    for (int i = 0; i < 4; i++) {
        parallel_dma_channels[i] = dma_claim_unused_channel(true);
        
        dma_channel_config config = dma_channel_get_default_config(parallel_dma_channels[i]);
        channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
        channel_config_set_read_increment(&config, true);
        channel_config_set_write_increment(&config, true);
        
        dma_channel_configure(
            parallel_dma_channels[i],
            &config,
            NULL,           // Write address (set for each operation)
            NULL,           // Read address (set for each operation)
            0,              // Transfer count (set for each operation)
            false           // Don't start yet
        );
    }
}

// RP2350: Parallel row scrolling for multiple rows at once
void parallel_scroll_rows(uint32_t* row_addrs, int16_t shift_amount, 
                         uint16_t width, uint8_t row_count) {
    // Process up to 4 rows in parallel
    uint8_t batch_count = (row_count + 3) / 4;
    
    for (uint8_t batch = 0; batch < batch_count; batch++) {
        uint8_t rows_in_batch = (batch == batch_count - 1 && row_count % 4 != 0) ? 
                              row_count % 4 : 4;
        
        // Start parallel DMA transfers
        for (uint8_t i = 0; i < rows_in_batch; i++) {
            uint8_t row_idx = batch * 4 + i;
            uint32_t* src_addr;
            uint32_t* dst_addr;
            uint32_t words_to_copy;
            
            if (shift_amount > 0) {
                // Shifting right
                src_addr = (uint32_t*)row_addrs[row_idx];
                dst_addr = (uint32_t*)(row_addrs[row_idx] + shift_amount);
                words_to_copy = (width - shift_amount + 3) / 4;
            } else {
                // Shifting left
                int shift = -shift_amount;
                src_addr = (uint32_t*)(row_addrs[row_idx] + shift);
                dst_addr = (uint32_t*)row_addrs[row_idx];
                words_to_copy = (width - shift + 3) / 4;
            }
            
            // Configure and start this DMA channel
            dma_channel_configure(
                parallel_dma_channels[i],
                &dma_channel_get_default_config(parallel_dma_channels[i]),
                dst_addr,       // Write address
                src_addr,       // Read address
                words_to_copy,  // Transfer count
                true            // Start immediately
            );
        }
        
        // Wait for all DMA transfers in this batch to complete
        for (uint8_t i = 0; i < rows_in_batch; i++) {
            dma_channel_wait_for_finish_blocking(parallel_dma_channels[i]);
        }
    }
}
#endif // RP2350
```

## Integration with Existing Code
Integrate the hardware acceleration with the main rendering functions:

```c
// Initialize hardware scrolling acceleration
void init_hw_scrolling() {
    // Initialize PIOs
    init_horizontal_scroll_pio();
    init_vertical_scroll_pio();
    init_tile_blit_pio();
    
    // Initialize DMA
    init_scroll_dma();
    
    #ifdef RP2350
    // RP2350-specific enhancements
    init_parallel_scroll_dma();
    #endif
    
    // Log initialization
    printf("Hardware scrolling acceleration initialized\n");
}

// Enhanced scroll_layer command implementation
void cmd_scroll_layer(uint8_t layer_id, uint16_t x_scroll, uint16_t y_scroll) {
    // Bounds checking
    if (layer_id >= MAX_LAYERS) return;
    
    // Calculate scroll delta
    int16_t dx = x_scroll - layers[layer_id].scroll_x;
    int16_t dy = y_scroll - layers[layer_id].scroll_y;
    
    // Use hardware acceleration for significant scrolling
    if (abs(dx) > 4 && abs(dx) < display_width / 2) {
        hw_scroll_horizontal(layer_id, dx);
    } else if (abs(dy) > 4 && abs(dy) < display_height / 2) {
        hw_scroll_vertical(layer_id, dy);
    } else {
        // Use regular software scrolling for small changes
        // or very large changes (full screen redraw)
        layers[layer_id].scroll_x = x_scroll;
        layers[layer_id].scroll_y = y_scroll;
        
        // Mark dirty regions as in original implementation
        // ...
    }
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SCROLL_LAYER);
}
```
