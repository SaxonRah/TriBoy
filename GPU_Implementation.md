# GPU Command Implementation on RP2040/RP2350
Base-line implementation details for the GPU command set, it covers the core functionality of the GPU microcontroller for the TriBoy system, providing:

1. An efficient command processing system
2. Background layer management with tile-based rendering
3. Sprite system with animation and transformation capabilities
4. Advanced effects (rotation, scaling, blending, scanline effects)
5. Memory optimization through dirty rectangle tracking and tile caching
6. Specific optimizations for both RP2040 and RP2350
7. Hardware acceleration using PIO blocks

The implementation focuses on maintaining high performance while providing rich features appropriate for a 16-bit style gaming system.

---

## System Command Implementation

### Reset GPU (0x01)
```c
void cmd_reset_gpu() {
    // Reset all state variables to defaults
    for (int i = 0; i < MAX_LAYERS; i++) {
        layers[i].enabled = false;
        layers[i].scroll_x = 0;
        layers[i].scroll_y = 0;
    }
    
    // Clear framebuffer
    memset(framebuffer, 0, framebuffer_size);
    
    // Reset sprite data
    memset(sprite_attributes, 0, sizeof(sprite_attributes));
    
    // Reset tile cache
    flush_tile_cache();
    
    // Send acknowledgment to CPU
    send_ack_to_cpu(CMD_RESET_GPU);
}
```

### Set Display Mode (0x02)
```c
void cmd_set_display_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    // Configure hardware
    if (bpp == 16) {
        // Configure PIO state machine for 16-bit RGB565 data
        pio_rgb565_init(pio0, 0, CONFIG_PIN_BASE);
    } else if (bpp == 8) {
        // Configure for 8-bit paletted mode
        pio_8bit_init(pio0, 0, CONFIG_PIN_BASE);
    }
    
    // Allocate framebuffer
    if (framebuffer != NULL) {
        free(framebuffer);
    }
    
    framebuffer_size = width * height * (bpp / 8);
    
    // For RP2040, make sure we don't exceed available RAM
    if (framebuffer_size > MAX_FRAMEBUFFER_SIZE) {
        // Fall back to lower resolution or BPP
        width = 256;
        height = 240;
        bpp = 8;
        framebuffer_size = width * height;
    }
    
    framebuffer = malloc(framebuffer_size);
    
    // Configure display parameters
    display_width = width;
    display_height = height;
    display_bpp = bpp;
    
    // Initialize rendering context
    init_render_context();
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SET_DISPLAY_MODE);
}
```

---

## Background Layer Implementation

### Configure Layer (0x20)
```c
void cmd_configure_layer(uint8_t layer_id, uint8_t enable, uint8_t priority,
                         uint8_t scroll_mode, uint8_t tile_width, uint8_t tile_height,
                         uint8_t width_tiles, uint8_t height_tiles) {
    // Bounds checking
    if (layer_id >= MAX_LAYERS) return;
    
    // Configure layer properties
    layers[layer_id].enabled = enable;
    layers[layer_id].priority = priority;
    layers[layer_id].scroll_mode = scroll_mode;
    layers[layer_id].tile_width = tile_width;
    layers[layer_id].tile_height = tile_height;
    layers[layer_id].width_tiles = width_tiles;
    layers[layer_id].height_tiles = height_tiles;
    
    // Allocate tilemap if needed
    if (layers[layer_id].tilemap != NULL) {
        free(layers[layer_id].tilemap);
    }
    
    layers[layer_id].tilemap = malloc(width_tiles * height_tiles * sizeof(TileInfo));
    memset(layers[layer_id].tilemap, 0, width_tiles * height_tiles * sizeof(TileInfo));
    
    // Mark entire layer as dirty for rendering
    mark_layer_dirty(layer_id);
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_CONFIGURE_LAYER);
}
```

### Load Tileset (0x21)
```c
void cmd_load_tileset(uint8_t layer_id, uint16_t start_index, uint16_t count, 
                     uint8_t compression, const uint8_t* data, uint32_t data_len) {
    // Bounds checking
    if (layer_id >= MAX_LAYERS) return;
    
    // Calculate size per tile
    uint32_t bytes_per_tile = (layers[layer_id].tile_width * layers[layer_id].tile_height * 
                              (layers[layer_id].bpp / 8));
    
    // Decompress if needed
    uint8_t* tile_data = data;
    uint8_t* decompressed = NULL;
    
    if (compression == 1) { // RLE
        decompressed = malloc(count * bytes_per_tile);
        uint32_t dec_size = rle_decompress(data, data_len, decompressed);
        tile_data = decompressed;
    }
    
    // Store in tile cache
    for (uint16_t i = 0; i < count; i++) {
        uint16_t tile_id = start_index + i;
        uint32_t offset = i * bytes_per_tile;
        
        // Add to tile cache (this may evict other tiles)
        cache_tile(layer_id, tile_id, &tile_data[offset], bytes_per_tile);
    }
    
    // Free decompressed data if needed
    if (decompressed != NULL) {
        free(decompressed);
    }
    
    // Mark layer as dirty since tile data changed
    mark_layer_dirty(layer_id);
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_LOAD_TILESET);
}
```

### Scroll Layer (0x23)
```c
void cmd_scroll_layer(uint8_t layer_id, uint16_t x_scroll, uint16_t y_scroll) {
    // Bounds checking
    if (layer_id >= MAX_LAYERS) return;
    
    // Store previous scroll values to determine dirty regions
    uint16_t prev_x = layers[layer_id].scroll_x;
    uint16_t prev_y = layers[layer_id].scroll_y;
    
    // Update scroll values
    layers[layer_id].scroll_x = x_scroll;
    layers[layer_id].scroll_y = y_scroll;
    
    // Calculate dirty regions based on scroll changes
    if (layers[layer_id].enabled) {
        // Horizontal scroll changed
        if (prev_x != x_scroll) {
            int16_t diff = abs((int16_t)prev_x - (int16_t)x_scroll);
            if (diff < display_width) {
                // Only part of the screen is dirty
                if (prev_x < x_scroll) {
                    // New area exposed on the right
                    mark_rect_dirty(display_width - diff, 0, diff, display_height);
                } else {
                    // New area exposed on the left
                    mark_rect_dirty(0, 0, diff, display_height);
                }
            } else {
                // Scroll distance exceeds screen width, entire screen dirty
                mark_rect_dirty(0, 0, display_width, display_height);
            }
        }
        
        // Vertical scroll changed
        if (prev_y != y_scroll) {
            int16_t diff = abs((int16_t)prev_y - (int16_t)y_scroll);
            if (diff < display_height) {
                // Only part of the screen is dirty
                if (prev_y < y_scroll) {
                    // New area exposed on the bottom
                    mark_rect_dirty(0, display_height - diff, display_width, diff);
                } else {
                    // New area exposed on the top
                    mark_rect_dirty(0, 0, display_width, diff);
                }
            } else {
                // Scroll distance exceeds screen height, entire screen dirty
                mark_rect_dirty(0, 0, display_width, display_height);
            }
        }
    }
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SCROLL_LAYER);
}
```

---

## Sprite System Implementation

### Load Sprite Pattern (0x40)
```c
void cmd_load_sprite_pattern(uint8_t pattern_id, uint8_t width, uint8_t height, 
                           uint8_t bpp, uint8_t compression, 
                           const uint8_t* data, uint32_t data_len) {
    // Calculate pattern size
    uint32_t pattern_size = width * 8 * height * 8 * (bpp / 8);
    
    // Check if we have enough memory in sprite data area
    if (sprite_data_used + pattern_size > SPRITE_DATA_SIZE) {
        // Try to free some sprite patterns
        garbage_collect_sprite_patterns();
        
        // If still not enough, return error
        if (sprite_data_used + pattern_size > SPRITE_DATA_SIZE) {
            send_error_to_cpu(CMD_LOAD_SPRITE_PATTERN, ERR_OUT_OF_MEMORY);
            return;
        }
    }
    
    // Decompress if needed
    uint8_t* pattern_data = data;
    uint8_t* decompressed = NULL;
    
    if (compression == 1) { // RLE
        decompressed = malloc(pattern_size);
        uint32_t dec_size = rle_decompress(data, data_len, decompressed);
        pattern_data = decompressed;
    }
    
    // Find spot in sprite data memory
    uint32_t offset = allocate_sprite_data(pattern_size);
    
    // Copy the data
    memcpy(&sprite_data[offset], pattern_data, pattern_size);
    
    // Update the pattern table
    sprite_patterns[pattern_id].width = width;
    sprite_patterns[pattern_id].height = height;
    sprite_patterns[pattern_id].bpp = bpp;
    sprite_patterns[pattern_id].data_offset = offset;
    sprite_patterns[pattern_id].data_size = pattern_size;
    sprite_patterns[pattern_id].in_use = true;
    
    // Free decompressed data if needed
    if (decompressed != NULL) {
        free(decompressed);
    }
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_LOAD_SPRITE_PATTERN);
}
```

### Define Sprite (0x41)
```c
void cmd_define_sprite(uint8_t sprite_id, uint8_t pattern_id, uint16_t x, uint16_t y,
                     uint8_t attributes, uint8_t palette_offset, uint8_t scale) {
    // Check if pattern exists
    if (!sprite_patterns[pattern_id].in_use) {
        send_error_to_cpu(CMD_DEFINE_SPRITE, ERR_INVALID_PATTERN);
        return;
    }
    
    // Check if sprite ID is valid
    if (sprite_id >= MAX_SPRITES) {
        send_error_to_cpu(CMD_DEFINE_SPRITE, ERR_INVALID_SPRITE_ID);
        return;
    }
    
    // If sprite was previously visible, mark its area as dirty
    if (sprites[sprite_id].visible) {
        mark_sprite_area_dirty(sprite_id);
    }
    
    // Update sprite attributes
    sprites[sprite_id].pattern_id = pattern_id;
    sprites[sprite_id].x = x;
    sprites[sprite_id].y = y;
    sprites[sprite_id].attributes = attributes;
    sprites[sprite_id].palette_offset = palette_offset;
    sprites[sprite_id].scale = scale;
    sprites[sprite_id].visible = true;
    
    // Mark new sprite area as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update sprite Y-order for rendering
    update_sprite_ordering();
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_DEFINE_SPRITE);
}
```

### Move Sprite (0x42)
```c
void cmd_move_sprite(uint8_t sprite_id, uint16_t x, uint16_t y) {
    // Check if sprite ID is valid
    if (sprite_id >= MAX_SPRITES || !sprites[sprite_id].visible) {
        send_error_to_cpu(CMD_MOVE_SPRITE, ERR_INVALID_SPRITE_ID);
        return;
    }
    
    // Mark old sprite position as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update position
    sprites[sprite_id].x = x;
    sprites[sprite_id].y = y;
    
    // Mark new sprite position as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update sprite ordering if Y changed
    update_sprite_ordering();
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_MOVE_SPRITE);
}
```

---

## Special Effects Implementation

### Set Fade (0x60)
```c
void cmd_set_fade(uint8_t mode, uint8_t level) {
    // Store fade parameters
    fade_mode = mode;
    fade_level = level;
    
    // If using hardware fade via PWM (display backlight)
    if (HARDWARE_FADE_SUPPORTED) {
        uint16_t pwm_value;
        
        if (mode == 0) { // Fade in
            pwm_value = ((uint16_t)level * PWM_MAX) / 255;
        } else { // Fade out
            pwm_value = PWM_MAX - ((uint16_t)level * PWM_MAX) / 255;
        }
        
        // Set backlight PWM
        pwm_set_gpio_level(BACKLIGHT_PIN, pwm_value);
    } else {
        // Use software fade - mark entire screen dirty to re-render with fade
        mark_rect_dirty(0, 0, display_width, display_height);
    }
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SET_FADE);
}
```

### Rotation/Zoom Background (0x63)
```c
void cmd_rotation_zoom_background(uint8_t layer_id, uint16_t angle, uint16_t scale_x, uint16_t scale_y) {
    // Bounds checking
    if (layer_id >= MAX_LAYERS) return;
    
    // Only allow rotation on one layer at a time (for RP2040 memory constraints)
    for (int i = 0; i < MAX_LAYERS; i++) {
        if (i != layer_id) {
            layers[i].rotation_enabled = false;
        }
    }
    
    // Convert angle from 0-1023 to radians
    float angle_rad = ((float)angle / 1023.0f) * 2.0f * 3.14159f;
    
    // Convert fixed-point scale (8.8) to float
    float sx = (float)scale_x / 256.0f;
    float sy = (float)scale_y / 256.0f;
    
    // Precompute sin/cos for rotation matrix
    float sin_a = sinf(angle_rad);
    float cos_a = cosf(angle_rad);
    
    // Store transformation matrix
    layers[layer_id].rotation_enabled = true;
    layers[layer_id].matrix[0] = cos_a * sx;
    layers[layer_id].matrix[1] = -sin_a * sx;
    layers[layer_id].matrix[2] = sin_a * sy;
    layers[layer_id].matrix[3] = cos_a * sy;
    
    // Calculate center of rotation
    layers[layer_id].rot_center_x = display_width / 2;
    layers[layer_id].rot_center_y = display_height / 2;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_ROTATION_ZOOM_BACKGROUND);
}
```

---

## Rendering Pipeline Implementation

### Core Rendering Loop (on Core 1)
```c
void core1_render_loop() {
    while (true) {
        // Wait for vsync or rendering request
        if (wait_for_render_signal()) {
            // Reset dirty region tracking
            init_dirty_regions();
            
            // Process any "copper list" commands
            process_copper_list();
            
            // Render each layer by priority
            for (int p = 0; p < MAX_PRIORITY; p++) {
                for (int l = 0; l < MAX_LAYERS; l++) {
                    if (layers[l].enabled && layers[l].priority == p) {
                        if (layers[l].rotation_enabled) {
                            render_rotated_layer(l);
                        } else {
                            render_layer(l);
                        }
                    }
                }
                
                // Render sprites at this priority
                render_sprites_at_priority(p);
            }
            
            // Apply special effects
            apply_post_effects();
            
            // Send frame to display
            send_frame_to_display();
            
            // Signal CPU that frame is complete
            if (vblank_callback_enabled) {
                gpio_put(VBLANK_PIN, 1);
                sleep_us(10);
                gpio_put(VBLANK_PIN, 0);
            }
        }
    }
}
```

### Layer Rendering (Normal)
```c
void render_layer(uint8_t layer_id) {
    Layer* layer = &layers[layer_id];
    
    // Only render the dirty regions
    for (int dr = 0; dr < dirty_region_count; dr++) {
        Rect* region = &dirty_regions[dr];
        
        // Calculate tile range for this dirty region
        int start_tile_x = (region->x + layer->scroll_x) / layer->tile_width;
        int start_tile_y = (region->y + layer->scroll_y) / layer->tile_height;
        int end_tile_x = ((region->x + region->width + layer->scroll_x) / layer->tile_width) + 1;
        int end_tile_y = ((region->y + region->height + layer->scroll_y) / layer->tile_height) + 1;
        
        // Wrap tiles if needed
        start_tile_x %= layer->width_tiles;
        start_tile_y %= layer->height_tiles;
        
        // Render each visible tile
        for (int ty = start_tile_y; ty <= end_tile_y; ty++) {
            int wrapped_ty = ty % layer->height_tiles;
            
            for (int tx = start_tile_x; tx <= end_tile_x; tx++) {
                int wrapped_tx = tx % layer->width_tiles;
                
                // Get tile info
                TileInfo* tile = &layer->tilemap[wrapped_ty * layer->width_tiles + wrapped_tx];
                
                // Skip empty tiles
                if (tile->tile_id == 0 && !tile->attributes) continue;
                
                // Get tile from cache
                uint8_t* tile_data = get_cached_tile(layer_id, tile->tile_id);
                if (tile_data == NULL) continue;
                
                // Calculate screen position for tile
                int screen_x = tx * layer->tile_width - layer->scroll_x;
                int screen_y = ty * layer->tile_height - layer->scroll_y;
                
                // Render the tile
                render_tile(screen_x, screen_y, tile_data, tile->attributes, 
                           layer->tile_width, layer->tile_height, layer->bpp);
            }
        }
    }
}
```

### Sprite Rendering
```c
void render_sprites_at_priority(uint8_t priority) {
    // Render sprites in Y-order (back to front)
    for (int i = 0; i < MAX_SPRITES; i++) {
        uint8_t sprite_id = sprite_order[i];
        
        // Skip inactive sprites or wrong priority
        if (!sprites[sprite_id].visible || 
            ((sprites[sprite_id].attributes >> 4) & 0x03) != priority) {
            continue;
        }
        
        // Get pattern info
        uint8_t pattern_id = sprites[sprite_id].pattern_id;
        SpritePattern* pattern = &sprite_patterns[pattern_id];
        
        // Skip if pattern not loaded
        if (!pattern->in_use) continue;
        
        // Calculate bounding box
        int width = pattern->width * 8;
        int height = pattern->height * 8;
        
        // Apply scaling
        if (sprites[sprite_id].scale != 128) { // 128 = 1.0 scale
            width = (width * sprites[sprite_id].scale) / 128;
            height = (height * sprites[sprite_id].scale) / 128;
        }
        
        // Screen coordinates (fixed-point 8.8 to pixel)
        int x = sprites[sprite_id].x >> 8;
        int y = sprites[sprite_id].y >> 8;
        
        // Check if sprite is within any dirty region
        bool in_dirty_region = false;
        for (int dr = 0; dr < dirty_region_count; dr++) {
            if (regions_overlap(x, y, width, height,
                              dirty_regions[dr].x, dirty_regions[dr].y,
                              dirty_regions[dr].width, dirty_regions[dr].height)) {
                in_dirty_region = true;
                break;
            }
        }
        
        // Skip if not in dirty region
        if (!in_dirty_region) continue;
        
        // Get sprite attributes
        uint8_t attributes = sprites[sprite_id].attributes;
        uint8_t palette_offset = sprites[sprite_id].palette_offset;
        
        // Get sprite data
        uint8_t* sprite_data = &sprite_data[pattern->data_offset];
        
        // Render the sprite
        render_sprite(x, y, width, height, sprite_data, attributes, palette_offset, pattern->bpp);
    }
}
```

---

## Sega Genesis-Inspired Features Implementation

### Dual Playfield Mode (0xC3)
```c
void cmd_set_dual_playfield(uint8_t enable) {
    dual_playfield_mode = enable;
    
    if (enable) {
        // Configure layers 0 and 1 for dual playfield
        layers[0].dual_playfield = true;
        layers[1].dual_playfield = true;
        
        // Ensure layers have correct priority
        layers[0].priority = 0;
        layers[1].priority = 1;
        
        // Set layer processing for dual playfield
        set_dual_playfield_processing();
    } else {
        // Disable dual playfield mode
        layers[0].dual_playfield = false;
        layers[1].dual_playfield = false;
    }
    
    // Mark screen dirty to re-render
    mark_rect_dirty(0, 0, display_width, display_height);
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SET_DUAL_PLAYFIELD);
}
```

### Sprite Collision Detection (0xC4)
```c
void cmd_set_sprite_collision_detection(uint8_t mode) {
    collision_detection_mode = mode;
    
    // Reset collision buffers if needed
    if (mode != 0) {
        memset(sprite_collision_buffer, 0, sizeof(sprite_collision_buffer));
        
        if (mode == 2 || mode == 3) {
            // For sprite-BG collisions, allocate buffer if needed
            if (bg_collision_buffer == NULL) {
                bg_collision_buffer = malloc(display_width * display_height / 8);
            }
            memset(bg_collision_buffer, 0, display_width * display_height / 8);
        }
    }
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SET_SPRITE_COLLISION_DETECTION);
}
```

---

## RP2350-Specific Enhancements

For the RP2350 with 520KB SRAM, we can implement several performance and feature enhancements:

```c
// Double buffering implementation
void init_double_buffering(void) {
    // Only available on RP2350 due to memory requirements
    #ifdef RP2350
    // Allocate two framebuffers
    front_buffer = malloc(framebuffer_size);
    back_buffer = malloc(framebuffer_size);
    
    // Initialize both
    memset(front_buffer, 0, framebuffer_size);
    memset(back_buffer, 0, framebuffer_size);
    
    // Set active buffer
    framebuffer = back_buffer;
    #endif
}

// Enhanced layer blending with 16-bit color
void apply_alpha_blend_16bit(uint8_t layer_id) {
    #ifdef RP2350
    Layer* layer = &layers[layer_id];
    uint16_t* fb = (uint16_t*)framebuffer;
    uint16_t* layer_data = (uint16_t*)layer->buffer;
    
    // Process only dirty regions for efficiency
    for (int dr = 0; dr < dirty_region_count; dr++) {
        Rect* region = &dirty_regions[dr];
        
        for (int y = region->y; y < region->y + region->height; y++) {
            for (int x = region->x; x < region->x + region->width; x++) {
                int idx = y * display_width + x;
                
                // Get colors
                uint16_t fb_color = fb[idx];
                uint16_t layer_color = layer_data[idx];
                
                // Skip transparent pixels
                if ((layer_color & 0xFFFF) == transparent_color) continue;
                
                // Extract RGB components
                uint8_t fb_r = (fb_color >> 11) & 0x1F;
                uint8_t fb_g = (fb_color >> 5) & 0x3F;
                uint8_t fb_b = fb_color & 0x1F;
                
                uint8_t layer_r = (layer_color >> 11) & 0x1F;
                uint8_t layer_g = (layer_color >> 5) & 0x3F;
                uint8_t layer_b = layer_color & 0x1F;
                
                // Blend based on alpha value
                uint8_t alpha = layer->alpha;
                uint8_t inv_alpha = 255 - alpha;
                
                uint8_t r = (fb_r * inv_alpha + layer_r * alpha) / 255;
                uint8_t g = (fb_g * inv_alpha + layer_g * alpha) / 255;
                uint8_t b = (fb_b * inv_alpha + layer_b * alpha) / 255;
                
                // Pack back into 16-bit color
                fb[idx] = (r << 11) | (g << 5) | b;
            }
        }
    }
    #endif
}
```

---

## Hardware Acceleration with PIO

The Programmable I/O (PIO) blocks are perfect for display interfacing and hardware acceleration:

```c
// Initialize display DMA using PIO
void init_display_pio(void) {
    // Configure state machines for display output
    uint offset = pio_add_program(pio0, &lcd_program);
    
    // Configure program
    lcd_program_init(pio0, 0, offset, LCD_PIN_DC, LCD_PIN_WR, LCD_PIN_DATA_BASE);
    
    // Set up DMA for display updates
    dma_channel = dma_claim_unused_channel(true);
    
    // Configure channel for memory-to-PIO transfer
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio0, 0, true));
    
    dma_channel_configure(
        dma_channel,
        &c,
        &pio0->txf[0],   // Write to PIO TX FIFO
        framebuffer,     // Read from framebuffer
        framebuffer_size / 2, // Transfer size (16-bit values)
        false            // Don't start yet
    );
}

// Hardware-accelerated horizontal line drawing with PIO
void init_hline_acceleration(void) {
    // Load horizontal line program into PIO
    uint offset = pio_add_program(pio1, &hline_program);
    hline_program_init(pio1, 1, offset);
    
    // This program efficiently draws horizontal lines by setting consecutive
    // pixels to the same color without CPU intervention
}
```

---

## Memory Management and Optimization

```c
// Tile Cache Implementation
void cache_tile(uint8_t layer_id, uint16_t tile_id, const uint8_t* data, uint32_t size) {
    // Check if already in cache
    for (int i = 0; i < tile_cache_count; i++) {
        if (tile_cache[i].layer_id == layer_id && 
            tile_cache[i].tile_id == tile_id) {
            // Already cached, just update LRU counter
            tile_cache[i].last_used = frame_counter;
            return;
        }
    }
    
    // Not in cache, find a slot
    int slot = -1;
    
    // If cache not full, use next empty slot
    if (tile_cache_count < MAX_CACHED_TILES) {
        slot = tile_cache_count++;
    } else {
        // Find least recently used tile
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < MAX_CACHED_TILES; i++) {
            if (tile_cache[i].last_used < oldest) {
                oldest = tile_cache[i].last_used;
                slot = i;
            }
        }
        
        // Free the previous data
        if (tile_cache[slot].data != NULL) {
            free(tile_cache[slot].data);
        }
    }
    
    // Allocate and store new tile
    tile_cache[slot].layer_id = layer_id;
    tile_cache[slot].tile_id = tile_id;
    tile_cache[slot].last_used = frame_counter;
    tile_cache[slot].data = malloc(size);
    tile_cache[slot].size = size;
    
    memcpy(tile_cache[slot].data, data, size);
}

void mark_rect_dirty(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    // Clip to screen bounds
    if (x >= display_width || y >= display_height) return;
    if (x + width > display_width) width = display_width - x;
    if (y + height > display_height) height = display_height - y;
    
    // Check if we can merge with existing dirty rect
    for (int i = 0; i < dirty_region_count; i++) {
        Rect* r = &dirty_regions[i];
        
        // Check if rectangles overlap or are adjacent
        if (regions_adjacent_or_overlap(r->x, r->y, r->width, r->height,
                                     x, y, width, height)) {
            // Merge rectangles
            uint16_t new_x = min(r->x, x);
            uint16_t new_y = min(r->y, y);
            uint16_t new_right = max(r->x + r->width, x + width);
            uint16_t new_bottom = max(r->y + r->height, y + height);
            
            r->x = new_x;
            r->y = new_y;
            r->width = new_right - new_x;
            r->height = new_bottom - new_y;
            
            // If area is too big, just mark whole screen dirty
            if (r->width * r->height > (display_width * display_height) / 2) {
                dirty_regions[0].x = 0;
                dirty_regions[0].y = 0;
                dirty_regions[0].width = display_width;
                dirty_regions[0].height = display_height;
                dirty_region_count = 1;
            }
            
            return;
        }
    }
    
    // If we can't merge and have space, add new dirty region
    if (dirty_region_count < MAX_DIRTY_REGIONS) {
        Rect* r = &dirty_regions[dirty_region_count++];
        r->x = x;
        r->y = y;
        r->width = width;
        r->height = height;
    } else {
        // Just mark whole screen dirty if too many regions
        dirty_regions[0].x = 0;
        dirty_regions[0].y = 0;
        dirty_regions[0].width = display_width;
        dirty_regions[0].height = display_height;
        dirty_region_count = 1;
    }
}
```

---

## Advanced Effects Implementation

### Rotation and Scaling (for RP2350)

```c
void render_rotated_layer(uint8_t layer_id) {
    Layer* layer = &layers[layer_id];
    
    // Get transformation matrix
    float m0 = layer->matrix[0];
    float m1 = layer->matrix[1];
    float m2 = layer->matrix[2];
    float m3 = layer->matrix[3];
    
    // Center of rotation
    int cx = layer->rot_center_x;
    int cy = layer->rot_center_y;
    
    // Process only dirty regions for efficiency
    for (int dr = 0; dr < dirty_region_count; dr++) {
        Rect* region = &dirty_regions[dr];
        
        for (int y = region->y; y < region->y + region->height; y++) {
            for (int x = region->x; x < region->x + region->width; x++) {
                // Calculate transformed coordinates
                float dx = x - cx;
                float dy = y - cy;
                
                // Apply rotation/scaling matrix
                float tx = dx * m0 + dy * m1 + cx;
                float ty = dx * m2 + dy * m3 + cy;
                
                // Add scroll offsets
                tx += layer->scroll_x;
                ty += layer->scroll_y;
                
                // Calculate source tile coordinates
                int tile_x = (int)tx / layer->tile_width;
                int tile_y = (int)ty / layer->tile_height;
                
                // Wrap to tilemap bounds
                tile_x %= layer->width_tiles;
                tile_y %= layer->height_tiles;
                if (tile_x < 0) tile_x += layer->width_tiles;
                if (tile_y < 0) tile_y += layer->height_tiles;
                
                // Get tile info
                TileInfo* tile = &layer->tilemap[tile_y * layer->width_tiles + tile_x];
                
                // Skip empty tiles
                if (tile->tile_id == 0 && !tile->attributes) continue;
                
                // Get tile data from cache
                uint8_t* tile_data = get_cached_tile(layer_id, tile->tile_id);
                if (tile_data == NULL) continue;
                
                // Calculate pixel within tile
                int px = ((int)tx % layer->tile_width);
                int py = ((int)ty % layer->tile_height);
                if (px < 0) px += layer->tile_width;
                if (py < 0) py += layer->tile_height;
                
                // Apply horizontal flip
                if (tile->attributes & TILE_ATTR_FLIP_X) {
                    px = layer->tile_width - 1 - px;
                }
                
                // Apply vertical flip
                if (tile->attributes & TILE_ATTR_FLIP_Y) {
                    py = layer->tile_height - 1 - py;
                }
                
                // Get pixel color from tile
                uint8_t color_idx;
                if (layer->bpp == 4) {
                    int byte_offset = (py * layer->tile_width + px) / 2;
                    if (px & 1) {
                        color_idx = tile_data[byte_offset] & 0x0F;
                    } else {
                        color_idx = (tile_data[byte_offset] >> 4) & 0x0F;
                    }
                } else if (layer->bpp == 8) {
                    color_idx = tile_data[py * layer->tile_width + px];
                } else { // 16-bit
                    uint16_t *color_ptr = (uint16_t *)(tile_data + (py * layer->tile_width + px) * 2);
                    plot_pixel_16bit(x, y, *color_ptr);
                    continue;
                }
                
                // Skip transparent pixels
                if (color_idx == 0) continue;
                
                // Apply palette offset from tile attributes
                color_idx += (tile->attributes & TILE_ATTR_PALETTE_MASK) >> TILE_ATTR_PALETTE_SHIFT;
                
                // Plot the pixel
                plot_pixel(x, y, color_idx);
            }
        }
    }
}
```

### Scanline Effects Implementation

```c
void cmd_scanline_effect(uint8_t mode, uint8_t line_count, const uint8_t* effect_values) {
    // Allocate or resize the scanline effect table if needed
    if (scanline_effects == NULL || scanline_effect_count != line_count) {
        if (scanline_effects != NULL) {
            free(scanline_effects);
        }
        scanline_effects = malloc(line_count * sizeof(ScanlineEffect));
        scanline_effect_count = line_count;
    }
    
    // Store the mode
    scanline_effect_mode = mode;
    
    // Parse the effect values based on mode
    if (mode == 0) { // Brightness
        for (int i = 0; i < line_count; i++) {
            scanline_effects[i].line = effect_values[i*2];
            scanline_effects[i].value = effect_values[i*2+1];
        }
    } else if (mode == 1) { // Color adjust
        for (int i = 0; i < line_count; i++) {
            scanline_effects[i].line = effect_values[i*4];
            scanline_effects[i].r = effect_values[i*4+1];
            scanline_effects[i].g = effect_values[i*4+2];
            scanline_effects[i].b = effect_values[i*4+3];
        }
    }
    
    // Enable the effect
    scanline_effect_enabled = true;
    
    // Mark whole screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SCANLINE_EFFECT);
}

// Apply the scanline effects during rendering
void apply_scanline_effects() {
    if (!scanline_effect_enabled || scanline_effects == NULL) return;
    
    // Process each scanline with an effect
    for (int i = 0; i < scanline_effect_count; i++) {
        ScanlineEffect* effect = &scanline_effects[i];
        int y = effect->line;
        
        // Skip if line is outside display
        if (y >= display_height) continue;
        
        if (scanline_effect_mode == 0) { // Brightness
            // Apply brightness adjustment to line
            uint8_t brightness = effect->value;
            
            if (display_bpp == 8) {
                uint8_t* line = framebuffer + y * display_width;
                
                for (int x = 0; x < display_width; x++) {
                    uint8_t color_idx = line[x];
                    
                    // Skip index 0 (transparent)
                    if (color_idx == 0) continue;
                    
                    // Get RGB components
                    RGB color = palette[color_idx];
                    
                    // Apply brightness
                    color.r = (color.r * brightness) / 255;
                    color.g = (color.g * brightness) / 255;
                    color.b = (color.b * brightness) / 255;
                    
                    // Find closest palette entry
                    line[x] = find_closest_color(color);
                }
            } else if (display_bpp == 16) {
                uint16_t* line = (uint16_t*)(framebuffer + y * display_width * 2);
                
                for (int x = 0; x < display_width; x++) {
                    uint16_t color = line[x];
                    
                    // Extract RGB components (RGB565)
                    uint8_t r = (color >> 11) & 0x1F;
                    uint8_t g = (color >> 5) & 0x3F;
                    uint8_t b = color & 0x1F;
                    
                    // Apply brightness
                    r = (r * brightness) / 255;
                    g = (g * brightness) / 255;
                    b = (b * brightness) / 255;
                    
                    // Recombine
                    line[x] = (r << 11) | (g << 5) | b;
                }
            }
        } else if (scanline_effect_mode == 1) { // Color adjust
            // Apply RGB adjustment to line
            int8_t r_adjust = effect->r;
            int8_t g_adjust = effect->g;
            int8_t b_adjust = effect->b;
            
            if (display_bpp == 8) {
                uint8_t* line = framebuffer + y * display_width;
                
                for (int x = 0; x < display_width; x++) {
                    uint8_t color_idx = line[x];
                    
                    // Skip index 0 (transparent)
                    if (color_idx == 0) continue;
                    
                    // Get RGB components
                    RGB color = palette[color_idx];
                    
                    // Apply color adjustment
                    int r = color.r + r_adjust;
                    int g = color.g + g_adjust;
                    int b = color.b + b_adjust;
                    
                    // Clamp to valid range
                    color.r = clamp(r, 0, 255);
                    color.g = clamp(g, 0, 255);
                    color.b = clamp(b, 0, 255);
                    
                    // Find closest palette entry
                    line[x] = find_closest_color(color);
                }
            } else if (display_bpp == 16) {
                uint16_t* line = (uint16_t*)(framebuffer + y * display_width * 2);
                
                for (int x = 0; x < display_width; x++) {
                    uint16_t color = line[x];
                    
                    // Extract RGB components (RGB565)
                    int r = ((color >> 11) & 0x1F) << 3;
                    int g = ((color >> 5) & 0x3F) << 2;
                    int b = (color & 0x1F) << 3;
                    
                    // Apply color adjustment
                    r = clamp(r + r_adjust, 0, 255);
                    g = clamp(g + g_adjust, 0, 255);
                    b = clamp(b + b_adjust, 0, 255);
                    
                    // Convert back to RGB565
                    r = r >> 3;
                    g = g >> 2;
                    b = b >> 3;
                    
                    // Recombine
                    line[x] = (r << 11) | (g << 5) | b;
                }
            }
        }
    }
}
```

---

## Core 0 SPI Command Processing Loop

```c
void core0_main() {
    // Initialize GPU hardware
    init_hardware();
    
    // Initialize display
    init_display();
    
    // Main loop
    while (true) {
        // Check for commands from CPU
        if (spi_is_readable(spi0)) {
            // Read command byte
            uint8_t cmd_id = spi_read_blocking(spi0, 0, &cmd_buffer[0], 1)[0];
            
            // Read command length
            uint8_t cmd_len = spi_read_blocking(spi0, 0, &cmd_buffer[1], 1)[0];
            
            // Read remaining bytes
            if (cmd_len > 2) {
                spi_read_blocking(spi0, 0, &cmd_buffer[2], cmd_len - 2);
            }
            
            // Process command
            process_command(cmd_id, cmd_buffer, cmd_len);
        }
        
        // Check if we need to run the copper list
        if (copper_list_enabled && vsync_occurred) {
            trigger_copper_execution();
            vsync_occurred = false;
        }
        
        // Process any urgent rendering tasks
        process_urgent_tasks();
        
        // Send any pending notifications back to CPU
        if (notification_pending) {
            send_notification_to_cpu();
        }
    }
}
```

---

## Command Processing Function

```c
void process_command(uint8_t cmd_id, const uint8_t *cmd_data, uint8_t cmd_len) {
    switch (cmd_id) {
        // System Commands
        case CMD_NOP:
            // No operation
            break;
        
        case CMD_RESET_GPU:
            cmd_reset_gpu();
            break;
        
        case CMD_SET_DISPLAY_MODE:
            {
                uint16_t width = (cmd_data[2] << 8) | cmd_data[3];
                uint16_t height = (cmd_data[4] << 8) | cmd_data[5];
                uint8_t bpp = cmd_data[6];
                cmd_set_display_mode(width, height, bpp);
            }
            break;
        
        // Background Layer Commands
        case CMD_CONFIGURE_LAYER:
            {
                uint8_t layer_id = cmd_data[2];
                uint8_t enable = cmd_data[3];
                uint8_t priority = cmd_data[4];
                uint8_t scroll_mode = cmd_data[5];
                uint8_t tile_width = cmd_data[6];
                uint8_t tile_height = cmd_data[7];
                uint8_t width_tiles = cmd_data[8];
                uint8_t height_tiles = cmd_data[9];
                
                cmd_configure_layer(layer_id, enable, priority, scroll_mode,
                                  tile_width, tile_height, width_tiles, height_tiles);
            }
            break;
        
        case CMD_LOAD_TILESET:
            {
                uint8_t layer_id = cmd_data[2];
                uint16_t start_index = (cmd_data[3] << 8) | cmd_data[4];
                uint16_t count = (cmd_data[5] << 8) | cmd_data[6];
                uint8_t compression = cmd_data[7];
                const uint8_t *data = &cmd_data[8];
                uint32_t data_len = cmd_len - 8;
                
                cmd_load_tileset(layer_id, start_index, count, compression, data, data_len);
            }
            break;
        
        case CMD_SCROLL_LAYER:
            {
                uint8_t layer_id = cmd_data[2];
                uint16_t x_scroll = (cmd_data[3] << 8) | cmd_data[4];
                uint16_t y_scroll = (cmd_data[5] << 8) | cmd_data[6];
                
                cmd_scroll_layer(layer_id, x_scroll, y_scroll);
            }
            break;
        
        // Add additional commands here...
        
        default:
            // Unknown command
            send_error_to_cpu(cmd_id, ERR_UNKNOWN_COMMAND);
            break;
    }
}
```
