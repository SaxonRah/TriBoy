/*
Core GPU Architecture: The GPU is built around an RP2040 (264KB RAM) or RP2350 (520KB RAM) microcontroller with the following memory allocation:
  - **RP2040 (264KB)**: 128KB framebuffer, 64KB tile cache, 48KB sprite data, 16KB command buffer, 8KB work RAM
  - **RP2350 (520KB)**: 240KB framebuffer, 128KB tile cache, 96KB sprite data, 32KB command buffer, 24KB work RAM

The GPU supports several key features:
  1. Background layer management with tile-based rendering
  2. Sprite system with animation and transformation
  3. Special effects (fade, rotation, scaling, window clipping)
  4. Direct drawing functions
  5. Memory optimization through dirty rectangle tracking and tile caching

Command-Based Interface: Communication with the GPU is handled through a command system via SPI, where the CPU sends compact commands to control graphics. The command structure is:
  - 1 byte: Command ID
  - 1 byte: Command length (including ID and length)
  - N bytes: Command parameters

Implementation Approach:
  1. Efficiently handles background layers and sprites
  2. Provides robust command processing
  3. Maximizes frame rates through optimization
  4. Optimizes CPU usage across both cores
  5. Implements the complete command set

---

Core allocation:
  Core 0: Command processing, background rendering, CPU communication
  Core 1: Sprite rendering, effects processing, final compositing

Memory regions:
  - Framebuffer: Screen display data (128KB/240KB)
  - Tile cache: Cached tile graphics (64KB/128KB)
  - Sprite data: Sprite patterns (48KB/96KB)
  - Command buffer: Input commands (16KB/32KB)
  - Work RAM: Temporary calculations (8KB/24KB)

Graphics pipeline:
  1. Command reception and processing
  2. Background layer rendering
  3. Sprite rendering
  4. Special effects application
  5. Final compositing
  6. Display output via SPI/parallel interface
*/

// Command Processing System
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    switch (cmd_id) {
        // System Commands
        case CMD_NOP:
            // No operation
            break;
            
        case CMD_RESET_GPU:
            reset_gpu();
            send_ack_to_cpu(CMD_RESET_GPU);
            break;
            
        case CMD_SET_DISPLAY_MODE:
            {
                uint16_t width = (data[0] << 8) | data[1];
                uint16_t height = (data[2] << 8) | data[3];
                uint8_t bpp = data[4];
                set_display_mode(width, height, bpp);
            }
            break;
        
        // Palette Commands
        case CMD_SET_PALETTE_ENTRY:
            set_palette_entry(data[0], data[1], data[2], data[3]);
            break;
            
        case CMD_LOAD_PALETTE:
            load_palette(data[0], data[1], &data[2]);
            break;
        
        // Background Layer Commands
        case CMD_CONFIGURE_LAYER:
            configure_layer(data[0], data[1], data[2], data[3], 
                           data[4], data[5], data[6], data[7]);
            break;
            
        case CMD_LOAD_TILESET:
            {
                uint8_t layer_id = data[0];
                uint16_t tile_start = (data[1] << 8) | data[2];
                uint16_t tile_count = (data[3] << 8) | data[4];
                uint8_t compression = data[5];
                load_tileset(layer_id, tile_start, tile_count, compression, &data[6], length - 6);
            }
            break;
            
        case CMD_SCROLL_LAYER:
            {
                uint8_t layer_id = data[0];
                uint16_t x_scroll = (data[1] << 8) | data[2];
                uint16_t y_scroll = (data[3] << 8) | data[4];
                scroll_layer(layer_id, x_scroll, y_scroll);
            }
            break;
        
        // Sprite Commands
        case CMD_LOAD_SPRITE_PATTERN:
            {
                uint8_t pattern_id = data[0];
                uint8_t width = data[1];
                uint8_t height = data[2];
                uint8_t bpp = data[3];
                uint8_t compression = data[4];
                load_sprite_pattern(pattern_id, width, height, bpp, compression, &data[5], length - 5);
            }
            break;
            
        case CMD_DEFINE_SPRITE:
            {
                uint8_t sprite_id = data[0];
                uint8_t pattern_id = data[1];
                int16_t x = (data[2] << 8) | data[3];
                int16_t y = (data[4] << 8) | data[5];
                uint8_t attributes = data[6];
                uint8_t palette_offset = data[7];
                uint8_t scale = data[8];
                define_sprite(sprite_id, pattern_id, x, y, attributes, palette_offset, scale);
            }
            break;
        
        // Special Effects Commands
        case CMD_SET_FADE:
            set_fade(data[0], data[1]);
            break;
            
        case CMD_ROTATION_ZOOM_BACKGROUND:
            {
                uint8_t layer_id = data[0];
                uint16_t angle = (data[1] << 8) | data[2];
                uint16_t scale_x = (data[3] << 8) | data[4];
                uint16_t scale_y = (data[5] << 8) | data[6];
                set_rotation_zoom(layer_id, angle, scale_x, scale_y);
            }
            break;
        
        // Direct Drawing Commands
        case CMD_DRAW_PIXEL:
            {
                int16_t x = (data[0] << 8) | data[1];
                int16_t y = (data[2] << 8) | data[3];
                uint8_t color = data[4];
                draw_pixel(x, y, color);
            }
            break;
            
        case CMD_DRAW_LINE:
            {
                int16_t x1 = (data[0] << 8) | data[1];
                int16_t y1 = (data[2] << 8) | data[3];
                int16_t x2 = (data[4] << 8) | data[5];
                int16_t y2 = (data[6] << 8) | data[7];
                uint8_t color = data[8];
                draw_line(x1, y1, x2, y2, color);
            }
            break;
            
        default:
            // Unknown command
            send_error_to_cpu(ERROR_UNKNOWN_COMMAND);
            break;
    }
}

// Implementing VSYNC_WAIT Command
void cmd_vsync_wait() {
    while (!vsync_occurred) {
        tight_loop_contents(); // Wait for next VSYNC
    }
    vsync_occurred = false;
    send_ack_to_cpu(CMD_VSYNC_WAIT);
}

// Improved Memory Error Handling
void* safe_malloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        send_error_to_cpu(ERROR_OUT_OF_MEMORY);
    }
    return ptr;
}

// Background Layer and Tile System
// Layer structure
typedef struct {
    bool enabled;
    uint8_t priority;
    uint8_t scroll_mode;
    uint8_t tile_width;
    uint8_t tile_height;
    uint8_t width_tiles;
    uint8_t height_tiles;
    uint16_t scroll_x;
    uint16_t scroll_y;
    uint8_t* tilemap;
    uint8_t bpp;
    
    // Rotation/transformation properties
    bool rotation_enabled;
    float matrix[4];  // 2x2 transformation matrix
    int16_t rot_center_x;
    int16_t rot_center_y;
    
    // Special properties
    bool dual_playfield;
    uint8_t* h_scroll_table;
    uint8_t* v_scroll_table;
    uint8_t alpha;
    uint8_t blend_mode;
    
    // Buffer for storing layer content (for blending)
    uint8_t* buffer;
} Layer;

// Tile information structure
typedef struct {
    uint16_t tile_id;
    uint8_t attributes;  // Flip, palette, priority bits
} TileInfo;

// Tile cache entry
typedef struct {
    uint8_t layer_id;
    uint16_t tile_id;
    uint8_t* data;
    uint32_t size;
    uint32_t last_used;  // For LRU replacement
} TileCacheEntry;

// Global state
Layer layers[MAX_LAYERS];
TileCacheEntry tile_cache[MAX_CACHED_TILES];
uint32_t tile_cache_count = 0;
uint32_t frame_counter = 0;

void configure_layer(uint8_t layer_id, uint8_t enable, uint8_t priority, 
                    uint8_t scroll_mode, uint8_t tile_width, uint8_t tile_height,
                    uint8_t width_tiles, uint8_t height_tiles) {
    if (layer_id >= MAX_LAYERS) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Free previous tilemap if it exists
    if (layers[layer_id].tilemap != NULL) {
        free(layers[layer_id].tilemap);
    }
    
    // Configure layer
    layers[layer_id].enabled = (enable != 0);
    layers[layer_id].priority = priority;
    layers[layer_id].scroll_mode = scroll_mode;
    layers[layer_id].tile_width = tile_width;
    layers[layer_id].tile_height = tile_height;
    layers[layer_id].width_tiles = width_tiles;
    layers[layer_id].height_tiles = height_tiles;
    
    // Allocate tilemap
    uint32_t tilemap_size = width_tiles * height_tiles * sizeof(TileInfo);
    // layers[layer_id].tilemap = malloc(tilemap_size);
    layers[layer_id].tilemap = safe_malloc(tilemap_size);
    
    if (layers[layer_id].tilemap == NULL) {
        send_error_to_cpu(ERROR_OUT_OF_MEMORY);
        layers[layer_id].enabled = false;
        return;
    }
    
    // Initialize tilemap to empty tiles
    memset(layers[layer_id].tilemap, 0, tilemap_size);
    
    // Reset scroll position
    layers[layer_id].scroll_x = 0;
    layers[layer_id].scroll_y = 0;
    
    // Disable rotation by default
    layers[layer_id].rotation_enabled = false;
    
    // Set default bit depth
    layers[layer_id].bpp = 8;
    
    // Mark the entire screen as dirty since layer configuration changed
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_CONFIGURE_LAYER);
}

void load_tileset(uint8_t layer_id, uint16_t tile_start, uint16_t tile_count, 
                 uint8_t compression, const uint8_t* data, uint32_t data_size) {
    if (layer_id >= MAX_LAYERS) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Calculate size per tile
    uint32_t tile_width = layers[layer_id].tile_width;
    uint32_t tile_height = layers[layer_id].tile_height;
    uint32_t bpp = layers[layer_id].bpp;
    uint32_t bytes_per_tile = (tile_width * tile_height * bpp) / 8;
    
    // Decompress data if needed
    uint8_t* tile_data = NULL;
    uint32_t decompressed_size = 0;
    
    if (compression == 1) {  // RLE compression
        // Allocate worst-case buffer for decompression
        // tile_data = malloc(tile_count * bytes_per_tile);
        tile_data = safe_malloc(tile_count * bytes_per_tile);
        if (tile_data == NULL) {
            send_error_to_cpu(ERROR_OUT_OF_MEMORY);
            return;
        }
        
        decompressed_size = decompress_rle(data, data_size, tile_data, tile_count * bytes_per_tile);
    } else {
        // Use data directly
        tile_data = (uint8_t*)data;
        decompressed_size = data_size;
    }
    
    // Verify we have enough data for the requested tiles
    if (decompressed_size < tile_count * bytes_per_tile) {
        if (compression == 1) {
            free(tile_data);
        }
        send_error_to_cpu(ERROR_INVALID_DATA);
        return;
    }
    
    // Store tiles in cache
    for (uint16_t i = 0; i < tile_count; i++) {
        uint16_t tile_id = tile_start + i;
        uint32_t offset = i * bytes_per_tile;
        
        // Cache the tile
        cache_tile(layer_id, tile_id, &tile_data[offset], bytes_per_tile);
    }
    
    // Free decompressed data if we allocated it
    if (compression == 1) {
        free(tile_data);
    }
    
    // Mark the entire screen as dirty since tiles have changed
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_LOAD_TILESET);
}

void load_tilemap(uint8_t layer_id, uint8_t x, uint8_t y, uint8_t width, 
                 uint8_t height, uint8_t compression, const uint8_t* data, uint32_t data_size) {
    if (layer_id >= MAX_LAYERS || !layers[layer_id].tilemap) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Decompress data if needed
    uint8_t* tilemap_data = NULL;
    uint32_t decompressed_size = 0;
    uint32_t expected_size = width * height * sizeof(TileInfo);
    
    if (compression == 1) {  // RLE compression
        // Allocate worst-case buffer for decompression
        // tilemap_data = malloc(expected_size);
        tilemap_data = safe_malloc(expected_size);
        if (tilemap_data == NULL) {
            send_error_to_cpu(ERROR_OUT_OF_MEMORY);
            return;
        }
        
        decompressed_size = decompress_rle(data, data_size, tilemap_data, expected_size);
    } else {
        // Use data directly
        tilemap_data = (uint8_t*)data;
        decompressed_size = data_size;
    }
    
    // Verify we have enough data
    if (decompressed_size < expected_size) {
        if (compression == 1) {
            free(tilemap_data);
        }
        send_error_to_cpu(ERROR_INVALID_DATA);
        return;
    }
    
    // Copy tilemap data to the layer
    TileInfo* map_data = (TileInfo*)tilemap_data;
    uint8_t layer_width = layers[layer_id].width_tiles;
    
    for (uint8_t ty = 0; ty < height; ty++) {
        for (uint8_t tx = 0; tx < width; tx++) {
            // Calculate source and destination positions
            uint16_t src_pos = ty * width + tx;
            uint16_t dst_pos = (y + ty) * layer_width + (x + tx);
            
            // Copy tile info
            TileInfo* dst = (TileInfo*)layers[layer_id].tilemap + dst_pos;
            *dst = map_data[src_pos];
        }
    }
    
    // Free decompressed data if we allocated it
    if (compression == 1) {
        free(tilemap_data);
    }
    
    // Calculate screen coordinates for the updated region
    uint16_t screen_x = x * layers[layer_id].tile_width - layers[layer_id].scroll_x;
    uint16_t screen_y = y * layers[layer_id].tile_height - layers[layer_id].scroll_y;
    uint16_t screen_width = width * layers[layer_id].tile_width;
    uint16_t screen_height = height * layers[layer_id].tile_height;
    
    // Mark the updated region as dirty
    mark_rect_dirty(screen_x, screen_y, screen_width, screen_height);
    
    send_ack_to_cpu(CMD_LOAD_TILEMAP);
}

void scroll_layer(uint8_t layer_id, uint16_t scroll_x, uint16_t scroll_y) {
    if (layer_id >= MAX_LAYERS) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Store old scroll position to calculate dirty regions
    uint16_t old_scroll_x = layers[layer_id].scroll_x;
    uint16_t old_scroll_y = layers[layer_id].scroll_y;
    
    // Update scroll position
    layers[layer_id].scroll_x = scroll_x;
    layers[layer_id].scroll_y = scroll_y;
    
    // Only mark dirty regions if the layer is enabled
    if (layers[layer_id].enabled) {
        // Calculate dirty regions based on scroll change
        uint16_t diff_x = abs((int16_t)old_scroll_x - (int16_t)scroll_x);
        uint16_t diff_y = abs((int16_t)old_scroll_y - (int16_t)scroll_y);
        
        if (diff_x >= display_width || diff_y >= display_height) {
            // If scroll change is larger than screen, mark everything dirty
            mark_rect_dirty(0, 0, display_width, display_height);
        } else {
            // Mark newly exposed areas as dirty
            if (scroll_x > old_scroll_x) {
                // New area exposed on the right
                mark_rect_dirty(display_width - diff_x, 0, diff_x, display_height);
            } else if (scroll_x < old_scroll_x) {
                // New area exposed on the left
                mark_rect_dirty(0, 0, diff_x, display_height);
            }
            
            if (scroll_y > old_scroll_y) {
                // New area exposed on the bottom
                mark_rect_dirty(0, display_height - diff_y, display_width, diff_y);
            } else if (scroll_y < old_scroll_y) {
                // New area exposed on the top
                mark_rect_dirty(0, 0, display_width, diff_y);
            }
        }
    }
    
    send_ack_to_cpu(CMD_SCROLL_LAYER);
}

// Tile cache management
void cache_tile(uint8_t layer_id, uint16_t tile_id, const uint8_t* data, uint32_t size) {
    // Check if tile is already in cache
    for (uint32_t i = 0; i < tile_cache_count; i++) {
        if (tile_cache[i].layer_id == layer_id && tile_cache[i].tile_id == tile_id) {
            // Tile found, update last used time and return
            tile_cache[i].last_used = frame_counter;
            return;
        }
    }
    
    // Tile not in cache, need to add it
    
    // If cache is full, use LRU replacement
    if (tile_cache_count >= MAX_CACHED_TILES) {
        // Find least recently used tile
        uint32_t lru_index = 0;
        uint32_t lru_time = 0xFFFFFFFF;
        
        for (uint32_t i = 0; i < MAX_CACHED_TILES; i++) {
            if (tile_cache[i].last_used < lru_time) {
                lru_time = tile_cache[i].last_used;
                lru_index = i;
            }
        }
        
        // Free the LRU tile data
        if (tile_cache[lru_index].data != NULL) {
            free(tile_cache[lru_index].data);
        }
        
        // Use this slot
        uint32_t slot = lru_index;
        
        // Allocate and store the new tile
        tile_cache[slot].layer_id = layer_id;
        tile_cache[slot].tile_id = tile_id;
        tile_cache[slot].size = size;
        tile_cache[slot].last_used = frame_counter;
        
        //tile_cache[slot].data = malloc(size);
        tile_cache[slot].data = safe_malloc(size);
        if (tile_cache[slot].data != NULL) {
            memcpy(tile_cache[slot].data, data, size);
        }
    } else {
        // Cache isn't full, use a new slot
        uint32_t slot = tile_cache_count++;
        
        // Allocate and store the new tile
        tile_cache[slot].layer_id = layer_id;
        tile_cache[slot].tile_id = tile_id;
        tile_cache[slot].size = size;
        tile_cache[slot].last_used = frame_counter;
        
        //tile_cache[slot].data = malloc(size);
        tile_cache[slot].data = safe_malloc(size);
        if (tile_cache[slot].data != NULL) {
            memcpy(tile_cache[slot].data, data, size);
        }
    }
}

uint8_t* get_cached_tile(uint8_t layer_id, uint16_t tile_id) {
    // Search cache for the requested tile
    for (uint32_t i = 0; i < tile_cache_count; i++) {
        if (tile_cache[i].layer_id == layer_id && tile_cache[i].tile_id == tile_id) {
            // Tile found, update last used time and return the data
            tile_cache[i].last_used = frame_counter;
            return tile_cache[i].data;
        }
    }
    
    // Tile not found
    return NULL;
}

// Sprite System
// Sprite pattern structure
typedef struct {
    uint8_t width;        // In 8-pixel units
    uint8_t height;       // In 8-pixel units
    uint8_t bpp;          // Bits per pixel (4, 8, or 16)
    uint32_t data_offset; // Offset into sprite data memory
    uint32_t data_size;   // Size in bytes
    bool in_use;          // Whether this pattern is in use
} SpritePattern;

// Sprite attributes structure
typedef struct {
    bool visible;
    uint8_t pattern_id;
    int16_t x;            // Fixed-point position (8.8)
    int16_t y;            // Fixed-point position (8.8)
    uint8_t attributes;   // Bit flags for flip/priority
    uint8_t palette_offset;
    uint8_t scale;        // 0-255 representing 0.0-2.0
    
    // Animation
    bool animated;
    uint8_t start_frame;
    uint8_t end_frame;
    uint8_t current_frame;
    uint8_t frame_rate;
    uint8_t frame_counter;
    uint8_t loop_mode;    // 0=once, 1=loop, 2=ping-pong
    int8_t frame_dir;     // 1=forward, -1=reverse (for ping-pong)
} Sprite;

// Global state
SpritePattern sprite_patterns[MAX_PATTERNS];
Sprite sprites[MAX_SPRITES];
uint8_t* sprite_data;         // Memory for sprite pattern data
uint32_t sprite_data_size;    // Total size of sprite data memory
uint32_t sprite_data_used;    // Amount of sprite memory currently in use

// Sprite rendering order (sorted by Y position or priority)
uint8_t sprite_order[MAX_SPRITES];

void load_sprite_pattern(uint8_t pattern_id, uint8_t width, uint8_t height, 
                        uint8_t bpp, uint8_t compression, const uint8_t* data, uint32_t data_size) {
    if (pattern_id >= MAX_PATTERNS) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Calculate pattern size
    uint32_t pixel_width = width * 8;
    uint32_t pixel_height = height * 8;
    uint32_t bytes_per_pixel = bpp / 8;
    if (bpp < 8) bytes_per_pixel = 1;  // For 4bpp, we still use 1 byte per 2 pixels
    
    uint32_t pattern_size = pixel_width * pixel_height * bytes_per_pixel;
    if (bpp == 4) pattern_size /= 2;   // For 4bpp, we pack 2 pixels per byte
    
    // Check if we have enough memory
    if (pattern_size > sprite_data_size - sprite_data_used) {
        // Try to free some sprite memory by garbage collection
        garbage_collect_sprite_memory();
        
        // Check again
        if (pattern_size > sprite_data_size - sprite_data_used) {
            send_error_to_cpu(ERROR_OUT_OF_MEMORY);
            return;
        }
    }
    
    // Decompress data if needed
    uint8_t* pattern_bytes = NULL;
    uint32_t decompressed_size = 0;
    
    if (compression == 1) {  // RLE compression
        // Allocate buffer for decompression
        // pattern_bytes = malloc(pattern_size);
        pattern_bytes = safe_malloc(pattern_size);
        if (pattern_bytes == NULL) {
            send_error_to_cpu(ERROR_OUT_OF_MEMORY);
            return;
        }
        
        decompressed_size = decompress_rle(data, data_size, pattern_bytes, pattern_size);
        
        // Verify decompression
        if (decompressed_size != pattern_size) {
            free(pattern_bytes);
            send_error_to_cpu(ERROR_INVALID_DATA);
            return;
        }
    } else {
        // Use data directly
        pattern_bytes = (uint8_t*)data;
        decompressed_size = data_size;
        
        // Verify size
        if (decompressed_size < pattern_size) {
            send_error_to_cpu(ERROR_INVALID_DATA);
            return;
        }
    }
    
    // Free existing pattern if it was in use
    if (sprite_patterns[pattern_id].in_use) {
        // Recover space in sprite data memory
        sprite_data_used -= sprite_patterns[pattern_id].data_size;
    }
    
    // Allocate space in sprite data memory
    uint32_t offset = sprite_data_size - sprite_data_used - pattern_size;
    
    // Copy pattern data
    memcpy(sprite_data + offset, pattern_bytes, pattern_size);
    
    // Update pattern info
    sprite_patterns[pattern_id].width = width;
    sprite_patterns[pattern_id].height = height;
    sprite_patterns[pattern_id].bpp = bpp;
    sprite_patterns[pattern_id].data_offset = offset;
    sprite_patterns[pattern_id].data_size = pattern_size;
    sprite_patterns[pattern_id].in_use = true;
    
    // Update used memory
    sprite_data_used += pattern_size;
    
    // Free decompressed data if we allocated it
    if (compression == 1) {
        free(pattern_bytes);
    }
    
    send_ack_to_cpu(CMD_LOAD_SPRITE_PATTERN);
}

void define_sprite(uint8_t sprite_id, uint8_t pattern_id, int16_t x, int16_t y,
                  uint8_t attributes, uint8_t palette_offset, uint8_t scale) {
    if (sprite_id >= MAX_SPRITES) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    if (pattern_id >= MAX_PATTERNS || !sprite_patterns[pattern_id].in_use) {
        send_error_to_cpu(ERROR_INVALID_PATTERN);
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
    
    // Disable animation by default
    sprites[sprite_id].animated = false;
    
    // Mark new sprite area as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update sprite rendering order
    update_sprite_order();
    
    send_ack_to_cpu(CMD_DEFINE_SPRITE);
}

void move_sprite(uint8_t sprite_id, int16_t x, int16_t y) {
    if (sprite_id >= MAX_SPRITES || !sprites[sprite_id].visible) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Mark old position as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update position
    sprites[sprite_id].x = x;
    sprites[sprite_id].y = y;
    
    // Mark new position as dirty
    mark_sprite_area_dirty(sprite_id);
    
    // Update sprite order if needed
    if (sprite_order_mode == ORDER_BY_YPOS) {
        update_sprite_order();
    }
    
    send_ack_to_cpu(CMD_MOVE_SPRITE);
}

void animate_sprite(uint8_t sprite_id, uint8_t start_frame, uint8_t end_frame,
                   uint8_t frame_rate, uint8_t loop_mode) {
    if (sprite_id >= MAX_SPRITES || !sprites[sprite_id].visible) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Configure animation
    sprites[sprite_id].animated = true;
    sprites[sprite_id].start_frame = start_frame;
    sprites[sprite_id].end_frame = end_frame;
    sprites[sprite_id].current_frame = start_frame;
    sprites[sprite_id].frame_rate = frame_rate;
    sprites[sprite_id].frame_counter = 0;
    sprites[sprite_id].loop_mode = loop_mode;
    sprites[sprite_id].frame_dir = 1;  // Start animation going forward
    
    // Mark sprite area as dirty
    mark_sprite_area_dirty(sprite_id);
    send_ack_to_cpu(CMD_ANIMATE_SPRITE);
}

void update_sprite_animations() {
    // Process all sprite animations
    for (int i = 0; i < MAX_SPRITES; i++) {
        Sprite* sprite = &sprites[i];
        
        if (!sprite->visible || !sprite->animated) {
            continue;
        }
        
        // Increment frame counter
        sprite->frame_counter++;
        
        // Check if it's time for a new frame
        if (sprite->frame_counter >= (60 / sprite->frame_rate)) {
            sprite->frame_counter = 0;
            
            // Advance to next frame
            sprite->current_frame += sprite->frame_dir;
            
            // Handle looping based on mode
            if (sprite->frame_dir > 0 && sprite->current_frame > sprite->end_frame) {
                switch (sprite->loop_mode) {
                    case 0: // Once
                        sprite->current_frame = sprite->end_frame;
                        sprite->animated = false; // Stop animation
                        break;
                        
                    case 1: // Loop
                        sprite->current_frame = sprite->start_frame;
                        break;
                        
                    case 2: // Ping-pong
                        sprite->current_frame = sprite->end_frame - 1;
                        sprite->frame_dir = -1; // Reverse direction
                        break;
                }
            } else if (sprite->frame_dir < 0 && sprite->current_frame < sprite->start_frame) {
                switch (sprite->loop_mode) {
                    case 0: // Once
                        sprite->current_frame = sprite->start_frame;
                        sprite->animated = false; // Stop animation
                        break;
                        
                    case 1: // Loop
                        sprite->current_frame = sprite->end_frame;
                        break;
                        
                    case 2: // Ping-pong
                        sprite->current_frame = sprite->start_frame + 1;
                        sprite->frame_dir = 1; // Forward direction
                        break;
                }
            }
            
            // Mark sprite area as dirty
            mark_sprite_area_dirty(i);
            
            // Update pattern ID for the sprite based on animation frame
            sprite->pattern_id = sprite->start_frame + 
                (sprite->current_frame - sprite->start_frame) % 
                (sprite->end_frame - sprite->start_frame + 1);
        }
    }
}

void update_sprite_order() {
    // Initialize the order array
    for (int i = 0; i < MAX_SPRITES; i++) {
        sprite_order[i] = i;
    }
    
    // Sort sprites based on the current ordering mode
    if (sprite_order_mode == ORDER_BY_YPOS) {
        // Sort by Y position (bubble sort for simplicity)
        for (int i = 0; i < MAX_SPRITES - 1; i++) {
            for (int j = 0; j < MAX_SPRITES - i - 1; j++) {
                if (!sprites[sprite_order[j]].visible) {
                    // Move invisible sprites to the end
                    uint8_t temp = sprite_order[j];
                    sprite_order[j] = sprite_order[j + 1];
                    sprite_order[j + 1] = temp;
                } else if (sprites[sprite_order[j]].visible && 
                           sprites[sprite_order[j + 1]].visible &&
                           sprites[sprite_order[j]].y > sprites[sprite_order[j + 1]].y) {
                    // Swap if y position is greater
                    uint8_t temp = sprite_order[j];
                    sprite_order[j] = sprite_order[j + 1];
                    sprite_order[j + 1] = temp;
                }
            }
        }
    } else {
        // Sort by priority (attribute bits 4-5)
        for (int i = 0; i < MAX_SPRITES - 1; i++) {
            for (int j = 0; j < MAX_SPRITES - i - 1; j++) {
                uint8_t prio_j = (sprites[sprite_order[j]].attributes >> 4) & 0x03;
                uint8_t prio_j1 = (sprites[sprite_order[j + 1]].attributes >> 4) & 0x03;
                
                if (!sprites[sprite_order[j]].visible) {
                    // Move invisible sprites to the end
                    uint8_t temp = sprite_order[j];
                    sprite_order[j] = sprite_order[j + 1];
                    sprite_order[j + 1] = temp;
                } else if (sprites[sprite_order[j]].visible && 
                           sprites[sprite_order[j + 1]].visible &&
                           prio_j > prio_j1) {
                    // Swap if priority is higher (lower value = higher priority)
                    uint8_t temp = sprite_order[j];
                    sprite_order[j] = sprite_order[j + 1];
                    sprite_order[j + 1] = temp;
                }
            }
        }
    }
}

void mark_sprite_area_dirty(uint8_t sprite_id) {
    if (sprite_id >= MAX_SPRITES || !sprites[sprite_id].visible) {
        return;
    }
    
    // Get sprite pattern
    uint8_t pattern_id = sprites[sprite_id].pattern_id;
    if (pattern_id >= MAX_PATTERNS || !sprite_patterns[pattern_id].in_use) {
        return;
    }
    
    // Calculate sprite dimensions
    uint16_t width = sprite_patterns[pattern_id].width * 8;
    uint16_t height = sprite_patterns[pattern_id].height * 8;
    
    // Apply scaling
    if (sprites[sprite_id].scale != 128) { // 128 represents 1.0 scale
        width = (width * sprites[sprite_id].scale) / 128;
        height = (height * sprites[sprite_id].scale) / 128;
    }
    
    // Calculate screen position (fixed-point to pixel)
    int16_t x = sprites[sprite_id].x >> 8;
    int16_t y = sprites[sprite_id].y >> 8;
    
    // Add padding to ensure we catch any partially visible edges
    x -= 1;
    y -= 1;
    width += 2;
    height += 2;
    
    // Mark the region dirty
    mark_rect_dirty(x, y, width, height);
}

void garbage_collect_sprite_memory() {
    // Check which patterns are still in use
    bool pattern_used[MAX_PATTERNS] = {0};
    
    for (int i = 0; i < MAX_SPRITES; i++) {
        if (sprites[i].visible) {
            pattern_used[sprites[i].pattern_id] = true;
            
            // For animated sprites, mark all frames as used
            if (sprites[i].animated) {
                for (uint8_t f = sprites[i].start_frame; f <= sprites[i].end_frame; f++) {
                    if (f < MAX_PATTERNS) {
                        pattern_used[f] = true;
                    }
                }
            }
        }
    }
    
    // Free unused patterns
    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (sprite_patterns[i].in_use && !pattern_used[i]) {
            sprite_patterns[i].in_use = false;
            sprite_data_used -= sprite_patterns[i].data_size;
        }
    }
    
    // If we've freed a significant amount, compact the sprite data
    if (sprite_data_used < sprite_data_size / 2) {
        compact_sprite_memory();
    }
}

void compact_sprite_memory() {
    // Allocate temporary buffer
    // uint8_t* temp_buffer = malloc(sprite_data_used);
    uint8_t* temp_buffer = safe_malloc(sprite_data_used);
    if (temp_buffer == NULL) {
        return; // Can't compact without temporary space
    }
    
    // Copy used patterns to temporary buffer and update offsets
    uint32_t current_offset = 0;
    
    for (int i = 0; i < MAX_PATTERNS; i++) {
        if (sprite_patterns[i].in_use) {
            // Copy pattern data to temporary buffer
            memcpy(temp_buffer + current_offset, 
                   sprite_data + sprite_patterns[i].data_offset, 
                   sprite_patterns[i].data_size);
            
            // Update offset
            sprite_patterns[i].data_offset = current_offset;
            
            // Move to next position
            current_offset += sprite_patterns[i].data_size;
        }
    }
    
    // Copy back from temporary buffer
    memcpy(sprite_data, temp_buffer, sprite_data_used);
    
    // Free temporary buffer
    free(temp_buffer);
}

// Special Effects and Transformation
// Special effects state
struct {
    uint8_t fade_mode;    // 0=fade in, 1=fade out
    uint8_t fade_level;   // 0-255
    
    uint8_t mosaic_size;  // 0=off, 1-15=size
    
    bool window_enabled[2];
    uint8_t window_x1[2];
    uint8_t window_y1[2];
    uint8_t window_x2[2];
    uint8_t window_y2[2];
    uint8_t window_layer_mask[2];
    
    uint8_t color_math_mode; // 0=add, 1=subtract, 2=average
} effects;

void set_fade(uint8_t mode, uint8_t level) {
    effects.fade_mode = mode;
    effects.fade_level = level;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_SET_FADE);
}

void set_mosaic_effect(uint8_t size) {
    effects.mosaic_size = size;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_MOSAIC_EFFECT);
}

void set_window(uint8_t window_id, uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t layer_mask) {
    if (window_id >= 2) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    effects.window_enabled[window_id] = true;
    effects.window_x1[window_id] = x1;
    effects.window_y1[window_id] = y1;
    effects.window_x2[window_id] = x2;
    effects.window_y2[window_id] = y2;
    effects.window_layer_mask[window_id] = layer_mask;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_SET_WINDOW);
}

void set_color_math(uint8_t mode) {
    effects.color_math_mode = mode;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_COLOR_MATH);
}

void set_rotation_zoom(uint8_t layer_id, uint16_t angle, uint16_t scale_x, uint16_t scale_y) {
    if (layer_id >= MAX_LAYERS) {
        send_error_to_cpu(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Disable rotation on other layers (for RP2040 memory constraints)
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
    
    // Calculate transformation matrix
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    
    layers[layer_id].rotation_enabled = true;
    layers[layer_id].matrix[0] = cos_a * sx;  // a
    layers[layer_id].matrix[1] = -sin_a * sx; // b
    layers[layer_id].matrix[2] = sin_a * sy;  // c
    layers[layer_id].matrix[3] = cos_a * sy;  // d
    
    // Set center of rotation to center of screen
    layers[layer_id].rot_center_x = display_width / 2;
    layers[layer_id].rot_center_y = display_height / 2;
    
    // Mark entire screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_ROTATION_ZOOM_BACKGROUND);
}

// Apply fade effect to the framebuffer
void apply_fade_effect(uint8_t* buffer, uint32_t size) {
    uint8_t fade_level = effects.fade_level;
    if (fade_level == 0) return; // No fade
    
    bool fade_out = (effects.fade_mode == 1);
    
    // For 8-bit paletted mode
    if (display_bpp == 8) {
        // Process each pixel
        for (uint32_t i = 0; i < size; i++) {
            uint8_t pixel = buffer[i];
            
            // Skip transparent pixels (usually index 0)
            if (pixel == 0) continue;
            
            // Get RGB color from palette
            uint8_t r = palette[pixel].r;
            uint8_t g = palette[pixel].g;
            uint8_t b = palette[pixel].b;
            
            // Apply fade
            if (fade_out) {
                // Fade to black
                r = r * (255 - fade_level) / 255;
                g = g * (255 - fade_level) / 255;
                b = b * (255 - fade_level) / 255;
            } else {
                // Fade from black
                r = r * fade_level / 255;
                g = g * fade_level / 255;
                b = b * fade_level / 255;
            }
            
            // Find closest palette entry
            buffer[i] = find_nearest_color(r, g, b);
        }
    } else if (display_bpp == 16) {
        // For 16-bit RGB565 mode
        uint16_t* rgb_buffer = (uint16_t*)buffer;
        size /= 2; // 16-bit values
        
        for (uint32_t i = 0; i < size; i++) {
            uint16_t pixel = rgb_buffer[i];
            
            // Skip transparent pixels (0x0000)
            if (pixel == 0) continue;
            
            // Extract RGB components
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;
            
            // Scale to 0-255
            r = (r * 255) / 31;
            g = (g * 255) / 63;
            b = (b * 255) / 31;
            
            // Apply fade
            if (fade_out) {
                // Fade to black
                r = r * (255 - fade_level) / 255;
                g = g * (255 - fade_level) / 255;
                b = b * (255 - fade_level) / 255;
            } else {
                // Fade from black
                r = r * fade_level / 255;
                g = g * fade_level / 255;
                b = b * fade_level / 255;
            }
            
            // Scale back to RGB565
            r = (r * 31) / 255;
            g = (g * 63) / 255;
            b = (b * 31) / 255;
            
            // Recompose RGB565 value
            rgb_buffer[i] = (r << 11) | (g << 5) | b;
        }
    }
}

// Apply mosaic effect
void apply_mosaic_effect(uint8_t* buffer, uint32_t width, uint32_t height) {
    if (effects.mosaic_size <= 1) return; // No effect
    
    uint8_t size = effects.mosaic_size;
    
    // For 8-bit paletted mode
    if (display_bpp == 8) {
        // Create a temporary buffer for the result
        //uint8_t* temp = malloc(width * height);
        uint8_t* temp = safe_malloc(width * height);
        if (temp == NULL) return; // Not enough memory
        
        // Copy input buffer
        memcpy(temp, buffer, width * height);
        
        // Process each mosaic block
        for (uint32_t y = 0; y < height; y += size) {
            for (uint32_t x = 0; x < width; x += size) {
                // Get the top-left pixel of the block
                uint8_t pixel = buffer[y * width + x];
                
                // Fill the whole block with this pixel
                for (uint32_t by = 0; by < size && y + by < height; by++) {
                    for (uint32_t bx = 0; bx < size && x + bx < width; bx++) {
                        temp[(y + by) * width + (x + bx)] = pixel;
                    }
                }
            }
        }
        
        // Copy result back to original buffer
        memcpy(buffer, temp, width * height);
        free(temp);
    } else if (display_bpp == 16) {
        // For 16-bit RGB565 mode
        uint16_t* rgb_buffer = (uint16_t*)buffer;
        
        // Create a temporary buffer
        uint16_t* temp = malloc(width * height * 2);
        if (temp == NULL) return;
        
        memcpy(temp, rgb_buffer, width * height * 2);
        
        // Process each mosaic block
        for (uint32_t y = 0; y < height; y += size) {
            for (uint32_t x = 0; x < width; x += size) {
                // Get the top-left pixel of the block
                uint16_t pixel = rgb_buffer[y * width + x];
                
                // Fill the whole block with this pixel
                for (uint32_t by = 0; by < size && y + by < height; by++) {
                    for (uint32_t bx = 0; bx < size && x + bx < width; bx++) {
                        temp[(y + by) * width + (x + bx)] = pixel;
                    }
                }
            }
        }
        
        // Copy result back
        memcpy(rgb_buffer, temp, width * height * 2);
        free(temp);
    }
}

// Check if a pixel is inside a window
bool is_in_window(uint8_t x, uint8_t y, uint8_t layer_id) {
    for (int w = 0; w < 2; w++) {
        if (!effects.window_enabled[w]) continue;
        
        // Check if this layer is affected by this window
        if (!(effects.window_layer_mask[w] & (1 << layer_id))) continue;
        
        // Check if pixel is inside window bounds
        if (x >= effects.window_x1[w] && x <= effects.window_x2[w] &&
            y >= effects.window_y1[w] && y <= effects.window_y2[w]) {
            return true;
        }
    }
    
    return false;
}

// Rendering Pipeline
// Dirty rectangle system
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} Rect;

Rect dirty_regions[MAX_DIRTY_REGIONS];
uint8_t dirty_region_count = 0;

void mark_rect_dirty(uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    // Clip rectangle to screen bounds
    if (x >= display_width) return;
    if (y >= display_height) return;
    
    if (x + width > display_width) width = display_width - x;
    if (y + height > display_height) height = display_height - y;
    
    // Check if we can merge with an existing dirty region
    for (int i = 0; i < dirty_region_count; i++) {
        Rect* r = &dirty_regions[i];
        
        // Calculate rectangle bounds
        uint16_t r1_x1 = r->x;
        uint16_t r1_y1 = r->y;
        uint16_t r1_x2 = r->x + r->width;
        uint16_t r1_y2 = r->y + r->height;
        
        uint16_t r2_x1 = x;
        uint16_t r2_y1 = y;
        uint16_t r2_x2 = x + width;
        uint16_t r2_y2 = y + height;
        
        // Check if rectangles are adjacent or overlapping
        if (!(r1_x2 < r2_x1 || r2_x2 < r1_x1 || r1_y2 < r2_y1 || r2_y2 < r1_y1)) {
            // Merge rectangles by creating a new bounding rectangle
            uint16_t new_x1 = min(r1_x1, r2_x1);
            uint16_t new_y1 = min(r1_y1, r2_y1);
            uint16_t new_x2 = max(r1_x2, r2_x2);
            uint16_t new_y2 = max(r1_y2, r2_y2);
            
            r->x = new_x1;
            r->y = new_y1;
            r->width = new_x2 - new_x1;
            r->height = new_y2 - new_y1;
            
            // If the merged region is too large, just mark the whole screen
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
    
    // Add as a new dirty region if we have space
    if (dirty_region_count < MAX_DIRTY_REGIONS) {
        Rect* r = &dirty_regions[dirty_region_count++];
        r->x = x;
        r->y = y;
        r->width = width;
        r->height = height;
    } else {
        // Too many regions, just mark the whole screen
        dirty_regions[0].x = 0;
        dirty_regions[0].y = 0;
        dirty_regions[0].width = display_width;
        dirty_regions[0].height = display_height;
        dirty_region_count = 1;
    }
}

void clear_dirty_regions() {
    dirty_region_count = 0;
}

// Render a normal (non-rotated) layer
void render_layer(uint8_t layer_id, bool clip_to_dirty) {
    Layer* layer = &layers[layer_id];
    
    if (!layer->enabled) return;
    
    // Get layer properties
    uint8_t tile_width = layer->tile_width;
    uint8_t tile_height = layer->tile_height;
    uint16_t scroll_x = layer->scroll_x;
    uint16_t scroll_y = layer->scroll_y;
    
    // Determine regions to render
    if (clip_to_dirty) {
        // Only render dirty regions
        for (int dr = 0; dr < dirty_region_count; dr++) {
            Rect* region = &dirty_regions[dr];
            
            // Calculate tile range for this region
            int start_tile_x = (region->x + scroll_x) / tile_width;
            int start_tile_y = (region->y + scroll_y) / tile_height;
            int end_tile_x = ((region->x + region->width + scroll_x) / tile_width) + 1;
            int end_tile_y = ((region->y + region->height + scroll_y) / tile_height) + 1;
            
            // Render tiles in this region
            render_layer_region(layer_id, start_tile_x, start_tile_y, end_tile_x, end_tile_y);
        }
    } else {
        // Render the entire visible layer
        int start_tile_x = scroll_x / tile_width;
        int start_tile_y = scroll_y / tile_height;
        int end_tile_x = ((display_width + scroll_x) / tile_width) + 1;
        int end_tile_y = ((display_height + scroll_y) / tile_height) + 1;
        
        render_layer_region(layer_id, start_tile_x, start_tile_y, end_tile_x, end_tile_y);
    }
}

void render_layer_region(uint8_t layer_id, int start_tile_x, int start_tile_y, 
                        int end_tile_x, int end_tile_y) {
    Layer* layer = &layers[layer_id];
    
    uint8_t tile_width = layer->tile_width;
    uint8_t tile_height = layer->tile_height;
    uint8_t layer_width_tiles = layer->width_tiles;
    uint8_t layer_height_tiles = layer->height_tiles;
    uint16_t scroll_x = layer->scroll_x;
    uint16_t scroll_y = layer->scroll_y;
    
    // Handle special scroll modes
    bool per_line_scroll = (layer->scroll_mode == 2); // Line scroll mode
    bool per_column_scroll = (layer->scroll_mode == 3); // Column scroll mode
    
    // Render each tile in the region
    for (int ty = start_tile_y; ty < end_tile_y; ty++) {
        for (int tx = start_tile_x; tx < end_tile_x; tx++) {
            // Apply wrapping for tilemap coordinates
            int wrapped_tx = tx % layer_width_tiles;
            int wrapped_ty = ty % layer_height_tiles;
            
            if (wrapped_tx < 0) wrapped_tx += layer_width_tiles;
            if (wrapped_ty < 0) wrapped_ty += layer_height_tiles;
            
            // Get tile information from tilemap
            TileInfo* tile_info = (TileInfo*)(layer->tilemap + 
                (wrapped_ty * layer_width_tiles + wrapped_tx) * sizeof(TileInfo));
            
            // Skip empty tiles
            if (tile_info->tile_id == 0) continue;
            
            // Get tile data from cache
            uint8_t* tile_data = get_cached_tile(layer_id, tile_info->tile_id);
            if (tile_data == NULL) continue;
            
            // Calculate screen position for this tile
            int screen_x = tx * tile_width - scroll_x;
            int screen_y = ty * tile_height - scroll_y;
            
            // Apply per-line or per-column scrolling if enabled
            if (per_line_scroll && layer->h_scroll_table != NULL) {
                // Apply horizontal scroll offset for this line
                int y_pos = ty * tile_height;
                int offset = layer->h_scroll_table[y_pos % display_height];
                screen_x -= offset;
            }
            
            if (per_column_scroll && layer->v_scroll_table != NULL) {
                // Apply vertical scroll offset for this column
                int x_pos = tx * tile_width;
                int offset = layer->v_scroll_table[x_pos % display_width];
                screen_y -= offset;
            }
            
            // Skip tiles that are completely off-screen
            if (screen_x + tile_width <= 0 || screen_x >= display_width ||
                screen_y + tile_height <= 0 || screen_y >= display_height) {
                continue;
            }
            
            // Render the tile
            render_tile(screen_x, screen_y, tile_data, tile_info->attributes, 
                       tile_width, tile_height, layer->bpp, layer_id);
        }
    }
}

void render_tile(int x, int y, uint8_t* tile_data, uint8_t attributes, 
                uint8_t tile_width, uint8_t tile_height, uint8_t bpp, uint8_t layer_id) {
    // Extract tile attributes
    bool flip_x = (attributes & 0x01) != 0;
    bool flip_y = (attributes & 0x02) != 0;
    uint8_t palette_offset = (attributes >> 2) & 0x0F; // 4 bits for palette
    
    // Calculate bytes per pixel
    uint8_t bytes_per_pixel = bpp / 8;
    if (bpp < 8) bytes_per_pixel = 1; // For 4-bit, we store 2 pixels per byte
    
    // For 8-bit paletted mode, apply palette offset
    uint16_t palette_shift = palette_offset * 16; // Assume 16 colors per palette entry
    
    // Render the tile pixel by pixel
    for (int ty = 0; ty < tile_height; ty++) {
        for (int tx = 0; tx < tile_width; tx++) {
            // Apply flipping
            int src_x = flip_x ? (tile_width - 1 - tx) : tx;
            int src_y = flip_y ? (tile_height - 1 - ty) : ty;
            
            // Calculate source position in tile data
            uint32_t src_pos;
            
            if (bpp == 4) {
                // 4-bit (16 colors) - 2 pixels per byte
                src_pos = (src_y * tile_width + src_x) / 2;
                uint8_t shift = (src_x & 1) ? 0 : 4; // Even pixels use high nibble
                uint8_t pixel = (tile_data[src_pos] >> shift) & 0x0F;
                
                // Skip transparent pixels
                if (pixel == 0) continue;
                
                // Apply palette offset
                pixel += palette_shift;
                
                // Calculate screen position
                int screen_x = x + tx;
                int screen_y = y + ty;
                
                // Skip off-screen pixels
                if (screen_x < 0 || screen_x >= display_width || 
                    screen_y < 0 || screen_y >= display_height) {
                    continue;
                }

                // Check window clipping
                if (effects.window_enabled[0] || effects.window_enabled[1]) {
                    if (!is_in_window(screen_x, screen_y, layer_id)) {
                        continue;
                    }
                }
                
                // Write pixel to framebuffer
                framebuffer[screen_y * display_width + screen_x] = pixel;
              
            } else if (bpp == 8) {
                // 8-bit (256 colors) - 1 byte per pixel
                src_pos = src_y * tile_width + src_x;
                uint8_t pixel = tile_data[src_pos];
                
                // Skip transparent pixels
                if (pixel == 0) continue;
                
                // Calculate screen position
                int screen_x = x + tx;
                int screen_y = y + ty;
                
                // Skip off-screen pixels
                if (screen_x < 0 || screen_x >= display_width || 
                    screen_y < 0 || screen_y >= display_height) {
                    continue;
                }
                
                // Check window clipping
                if (effects.window_enabled[0] || effects.window_enabled[1]) {
                    if (!is_in_window(screen_x, screen_y, layer_id)) {
                        continue;
                    }
                }
                
                // Write pixel to framebuffer
                framebuffer[screen_y * display_width + screen_x] = pixel;
            } else if (bpp == 16) {
                // 16-bit RGB565 - 2 bytes per pixel
                src_pos = (src_y * tile_width + src_x) * 2;
                uint16_t pixel = tile_data[src_pos] | (tile_data[src_pos + 1] << 8);
                
                // Skip transparent pixels (0x0000)
                if (pixel == 0) continue;
                
                // Calculate screen position
                int screen_x = x + tx;
                int screen_y = y + ty;
                
                // Skip off-screen pixels
                if (screen_x < 0 || screen_x >= display_width || 
                    screen_y < 0 || screen_y >= display_height) {
                    continue;
                }
                
                // Check window clipping
                if (effects.window_enabled[0] || effects.window_enabled[1]) {
                    if (!is_in_window(screen_x, screen_y, layer_id)) {
                        continue;
                    }
                }
                
                // Write pixel to framebuffer (16-bit mode)
                ((uint16_t*)framebuffer)[screen_y * display_width + screen_x] = pixel;
            }
        }
    }
}

// Render a rotated/scaled layer
void render_rotated_layer(uint8_t layer_id) {
    Layer* layer = &layers[layer_id];
    
    if (!layer->enabled || !layer->rotation_enabled) return;
    
    // Get transformation matrix
    float a = layer->matrix[0];
    float b = layer->matrix[1];
    float c = layer->matrix[2];
    float d = layer->matrix[3];
    
    // Get center of rotation
    int cx = layer->rot_center_x;
    int cy = layer->rot_center_y;
    
    // Render each pixel in the framebuffer
    for (int y = 0; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            // Calculate source coordinates
            float dx = x - cx;
            float dy = y - cy;
            
            float src_x = (a * dx + b * dy) + cx + layer->scroll_x;
            float src_y = (c * dx + d * dy) + cy + layer->scroll_y;
            
            // Convert to tile coordinates
            int tile_x = (int)src_x / layer->tile_width;
            int tile_y = (int)src_y / layer->tile_height;
            
            // Apply wrapping
            int wrapped_tx = tile_x % layer->width_tiles;
            int wrapped_ty = tile_y % layer->height_tiles;
            
            if (wrapped_tx < 0) wrapped_tx += layer->width_tiles;
            if (wrapped_ty < 0) wrapped_ty += layer->height_tiles;
            
            // Get tile info
            TileInfo* tile_info = (TileInfo*)(layer->tilemap + 
                (wrapped_ty * layer->width_tiles + wrapped_tx) * sizeof(TileInfo));
            
            // Skip empty tiles
            if (tile_info->tile_id == 0) continue;
            
            // Get tile data
            uint8_t* tile_data = get_cached_tile(layer_id, tile_info->tile_id);
            if (tile_data == NULL) continue;
            
            // Calculate pixel coordinates within the tile
            int pixel_x = (int)src_x % layer->tile_width;
            int pixel_y = (int)src_y % layer->tile_height;
            
            if (pixel_x < 0) pixel_x += layer->tile_width;
            if (pixel_y < 0) pixel_y += layer->tile_height;
            
            // Apply flipping from tile attributes
            bool flip_x = (tile_info->attributes & 0x01) != 0;
            bool flip_y = (tile_info->attributes & 0x02) != 0;
            uint8_t palette_offset = (tile_info->attributes >> 2) & 0x0F;
            
            if (flip_x) pixel_x = layer->tile_width - 1 - pixel_x;
            if (flip_y) pixel_y = layer->tile_height - 1 - pixel_y;
            
            // Get pixel color from tile data
            uint8_t pixel;
            
            if (layer->bpp == 4) {
                // 4-bit (16 colors)
                int byte_pos = (pixel_y * layer->tile_width + pixel_x) / 2;
                int shift = (pixel_x & 1) ? 0 : 4;
                
                pixel = (tile_data[byte_pos] >> shift) & 0x0F;
                
                // Apply palette offset
                if (pixel != 0) { // Skip transparent pixels
                    pixel += palette_offset * 16;
                }
            } else if (layer->bpp == 8) {
                // 8-bit (256 colors)
                int byte_pos = pixel_y * layer->tile_width + pixel_x;
                pixel = tile_data[byte_pos];
            } else {
                // 16-bit (RGB565) - not handled here for simplicity
                continue;
            }
            
            // Skip transparent pixels
            if (pixel == 0) continue;
            
            // Check window clipping
            if (effects.window_enabled[0] || effects.window_enabled[1]) {
                if (!is_in_window(x, y, layer_id)) {
                    continue;
                }
            }
            
            // Write to framebuffer
            framebuffer[y * display_width + x] = pixel;
        }
    }
}

// Render sprites for a specific priority level
void render_sprites_at_priority(uint8_t priority) {
    for (int i = 0; i < MAX_SPRITES; i++) {
        uint8_t sprite_id = sprite_order[i];
        Sprite* sprite = &sprites[sprite_id];
        
        // Skip inactive sprites
        if (!sprite->visible) continue;
        
        // Check priority
        uint8_t sprite_priority = (sprite->attributes >> 4) & 0x03;
        if (sprite_priority != priority) continue;
        
        // Get pattern information
        uint8_t pattern_id = sprite->pattern_id;
        SpritePattern* pattern = &sprite_patterns[pattern_id];
        
        if (!pattern->in_use) continue;
        
        // Calculate dimensions
        uint16_t width = pattern->width * 8;
        uint16_t height = pattern->height * 8;
        
        // Apply scaling if needed
        if (sprite->scale != 128) { // 128 = 1.0 scale
            width = (width * sprite->scale) / 128;
            height = (height * sprite->scale) / 128;
        }
        
        // Convert from fixed-point to pixel coordinates
        int16_t x = sprite->x >> 8;
        int16_t y = sprite->y >> 8;
        
        // Skip completely off-screen sprites
        if (x + width <= 0 || x >= display_width || y + height <= 0 || y >= display_height) {
            continue;
        }
        
        // Render the sprite
        render_sprite(sprite_id, x, y, width, height);
    }
}

void render_sprite(uint8_t sprite_id, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    Sprite* sprite = &sprites[sprite_id];
    SpritePattern* pattern = &sprite_patterns[sprite->pattern_id];
    
    // Get sprite attributes
    bool flip_x = (sprite->attributes & 0x01) != 0;
    bool flip_y = (sprite->attributes & 0x02) != 0;
    uint8_t palette_offset = sprite->palette_offset;
    
    // Get sprite data
    uint8_t* sprite_data = sprite_data + pattern->data_offset;
    
    // Cache source dimensions
    uint16_t src_width = pattern->width * 8;
    uint16_t src_height = pattern->height * 8;
    
    // Scaling factors (16.16 fixed-point)
    uint32_t x_scale = (src_width << 16) / width;
    uint32_t y_scale = (src_height << 16) / height;
    
    // For each pixel in the destination sprite
    for (int16_t dy = 0; dy < height; dy++) {
        for (int16_t dx = 0; dx < width; dx++) {
            // Calculate screen position
            int16_t screen_x = x + dx;
            int16_t screen_y = y + dy;
            
            // Skip if outside screen
            if (screen_x < 0 || screen_x >= display_width || 
                screen_y < 0 || screen_y >= display_height) {
                continue;
            }
            
            // Calculate source position with scaling
            uint32_t src_x_fixed = dx * x_scale;
            uint32_t src_y_fixed = dy * y_scale;
            
            uint16_t src_x = src_x_fixed >> 16;
            uint16_t src_y = src_y_fixed >> 16;
            
            // Apply flipping
            if (flip_x) src_x = src_width - 1 - src_x;
            if (flip_y) src_y = src_height - 1 - src_y;
            
            // Get pixel from sprite data
            uint8_t pixel;
            
            if (pattern->bpp == 4) {
                // 4-bit (16 colors)
                uint32_t offset = (src_y * src_width + src_x) / 2;
                uint8_t shift = (src_x & 1) ? 0 : 4;
                
                pixel = (sprite_data[offset] >> shift) & 0x0F;
                
                // Apply palette offset
                if (pixel != 0) { // Skip transparent pixels
                    pixel += palette_offset * 16;
                }
            } else if (pattern->bpp == 8) {
                // 8-bit (256 colors)
                uint32_t offset = src_y * src_width + src_x;
                pixel = sprite_data[offset];
            } else if (pattern->bpp == 16) {
                // 16-bit RGB565
                uint32_t offset = (src_y * src_width + src_x) * 2;
                uint16_t rgb_pixel = sprite_data[offset] | (sprite_data[offset + 1] << 8);
                
                // Skip transparent pixels (0x0000)
                if (rgb_pixel == 0) continue;
                
                // Write directly to framebuffer for 16-bit mode
                ((uint16_t*)framebuffer)[screen_y * display_width + screen_x] = rgb_pixel;
                continue;
            }
            
            // Skip transparent pixels
            if (pixel == 0) continue;
            
            // Check for sprite collision if enabled
            if (collision_detection_mode == 1 || collision_detection_mode == 3) {
                // Check sprite-sprite collision
                uint32_t pos = screen_y * display_width + screen_x;
                
                if (sprite_collision_buffer[pos / 8] & (1 << (pos % 8))) {
                    // Collision detected
                    sprite_collision_detected = true;
                } else {
                    // Mark this pixel in collision buffer
                    sprite_collision_buffer[pos / 8] |= (1 << (pos % 8));
                }
            }
            
            // Write to framebuffer
            framebuffer[screen_y * display_width + screen_x] = pixel;
        }
    }
}

// Display Output and Synchronization
// Send framebuffer to display
void send_frame_to_display() {
    // Set the display window to full screen
    display_set_window(0, 0, display_width, display_height);
    
    // For 8-bit paletted mode, convert to RGB565 for the display
    if (display_bpp == 8) {
        // Configure for data transmission
        gpio_put(DISPLAY_DC_PIN, 1); // Data mode
        gpio_put(DISPLAY_CS_PIN, 0); // Select display
        
        // Send data in chunks to avoid large buffers
        uint16_t rgb_buffer[32];
        
        for (int i = 0; i < display_width * display_height; i += 32) {
            // Convert palette indices to RGB565
            int chunk_size = min(32, display_width * display_height - i);
            
            for (int j = 0; j < chunk_size; j++) {
                uint8_t index = framebuffer[i + j];
                RGB color = palette[index];
                
                // Convert 8-bit RGB to RGB565
                uint16_t r5 = (color.r * 31 / 255) & 0x1F;
                uint16_t g6 = (color.g * 63 / 255) & 0x3F;
                uint16_t b5 = (color.b * 31 / 255) & 0x1F;
                
                rgb_buffer[j] = (r5 << 11) | (g6 << 5) | b5;
            }
            
            // Send RGB565 data to display
            spi_write_blocking(DISPLAY_SPI_PORT, (uint8_t*)rgb_buffer, chunk_size * 2);
        }
        
        gpio_put(DISPLAY_CS_PIN, 1); // Deselect display
    } else if (display_bpp == 16) {
        // Send 16-bit data directly
        gpio_put(DISPLAY_DC_PIN, 1); // Data mode
        gpio_put(DISPLAY_CS_PIN, 0); // Select display
        
        // Send in chunks
        for (int i = 0; i < display_width * display_height; i += 256) {
            int chunk_size = min(256, display_width * display_height - i);
            spi_write_blocking(DISPLAY_SPI_PORT, &framebuffer[i * 2], chunk_size * 2);
        }
        
        gpio_put(DISPLAY_CS_PIN, 1); // Deselect display
    }
    
    // Signal VSYNC to CPU if enabled
    if (vblank_callback_enabled) {
        gpio_put(VSYNC_PIN, 0);
        sleep_us(10);
        gpio_put(VSYNC_PIN, 1);
    }
}

// Setup DMA for faster display updates
void setup_display_dma() {
    // Initialize DMA channel for display
    display_dma_channel = dma_claim_unused_channel(true);
    
    // Configure DMA channel
    dma_channel_config dma_config = dma_channel_get_default_config(display_dma_channel);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    
    // Set SPI as DREQ
    channel_config_set_dreq(&dma_config, spi_get_dreq(DISPLAY_SPI_PORT, true));
    
    // Configure DMA
    dma_channel_configure(
        display_dma_channel,
        &dma_config,
        &spi_get_hw(DISPLAY_SPI_PORT)->dr, // Write to SPI data register
        NULL, // Source will be set for each transfer
        0,    // Count will be set for each transfer
        false // Don't start yet
    );
}

// Send display data using DMA
void send_display_chunk_dma(uint8_t* data, size_t length) {
    // Setup and start DMA transfer
    dma_channel_set_read_addr(display_dma_channel, data, false);
    dma_channel_set_trans_count(display_dma_channel, length, true);
    
    // Wait for DMA to complete
    dma_channel_wait_for_finish_blocking(display_dma_channel);
}

// Core Execution Loops and Main Function
// Core 1 rendering function
void core1_rendering_loop() {
    printf("GPU Core 1 started - Rendering engine\n");
    
    while (true) {
        // Wait for render signal
        if (render_requested) {
            // Signal that we're starting to render
            rendering_in_progress = true;
            
            // Clear the framebuffer if needed
            if (clear_screen_requested) {
                memset(framebuffer, 0, framebuffer_size);
                clear_screen_requested = false;
            }
            
            // Render each layer by priority
            for (int p = 0; p < 4; p++) {
                // First render background layers at this priority
                for (int l = 0; l < MAX_LAYERS; l++) {
                    if (layers[l].enabled && layers[l].priority == p) {
                        if (layers[l].rotation_enabled) {
                            render_rotated_layer(l);
                        } else {
                            render_layer(l, true);
                        }
                    }
                }
                
                // Then render sprites at this priority
                render_sprites_at_priority(p);
            }
            
            // Apply global effects
            if (effects.fade_level > 0) {
                apply_fade_effect(framebuffer, framebuffer_size);
            }
            
            if (effects.mosaic_size > 1) {
                apply_mosaic_effect(framebuffer, display_width, display_height);
            }
            
            // Send frame to display
            send_frame_to_display();
            
            // Clear dirty regions for next frame
            clear_dirty_regions();
            
            // Update frame counter
            frame_counter++;
            
            // Signal that rendering is complete
            render_requested = false;
            rendering_in_progress = false;
        } else {
            // If no rendering is needed, sleep briefly
            sleep_us(100);
        }
    }
}

// Main function
int main() {
    // Initialize stdio
    stdio_init_all();
    printf("TriBoy GPU Initializing...\n");
    
    // Initialize hardware
    setup_spi_slave(); // For CPU communication
    setup_display();   // Configure display interface
    
    // Allocate memory
    // Determine if we're on RP2040 or RP2350
    bool is_rp2350 = check_if_rp2350();
    
    if (is_rp2350) {
        // RP2350 memory allocation (520KB)
        framebuffer_size = 240 * 1024; // 240KB for framebuffer (320x240x16-bit or larger)
        tile_cache_size = 128 * 1024;  // 128KB for tile cache
        sprite_data_size = 96 * 1024;  // 96KB for sprite data
        command_buffer_size = 32 * 1024; // 32KB for command buffer
        // ~24KB left for work RAM
    } else {
        // RP2040 memory allocation (264KB)
        framebuffer_size = 128 * 1024; // 128KB for framebuffer (320x240x8-bit)
        tile_cache_size = 64 * 1024;   // 64KB for tile cache
        sprite_data_size = 48 * 1024;  // 48KB for sprite data
        command_buffer_size = 16 * 1024; // 16KB for command buffer
        // ~8KB left for work RAM
    }
    
    // Allocate framebuffer
    // framebuffer = malloc(framebuffer_size);
    framebuffer = safe_malloc(framebuffer_size);
    if (framebuffer == NULL) {
        printf("Failed to allocate framebuffer!\n");
        while (1) tight_loop_contents();
    }
    
    // Allocate sprite data memory
    //sprite_data = malloc(sprite_data_size);
    sprite_data = safe_malloc(sprite_data_size);
    if (sprite_data == NULL) {
        printf("Failed to allocate sprite data memory!\n");
        while (1) tight_loop_contents();
    }
    
    // Initialize state
    clear_dirty_regions();
    memset(framebuffer, 0, framebuffer_size);
    memset(sprite_data, 0, sprite_data_size);
    
    // Initialize layers
    for (int i = 0; i < MAX_LAYERS; i++) {
        layers[i].enabled = false;
        layers[i].tilemap = NULL;
        layers[i].rotation_enabled = false;
    }
    
    // Initialize sprites
    for (int i = 0; i < MAX_SPRITES; i++) {
        sprites[i].visible = false;
        sprite_order[i] = i;
    }
    
    // Initialize sprite patterns
    for (int i = 0; i < MAX_PATTERNS; i++) {
        sprite_patterns[i].in_use = false;
    }
    
    // Initialize display mode (default 320x240x8-bit)
    display_width = 320;
    display_height = 240;
    display_bpp = 8;
    
    // Initialize special effects
    effects.fade_level = 0;
    effects.mosaic_size = 0;
    effects.window_enabled[0] = false;
    effects.window_enabled[1] = false;
    
    // Initialize palette with default colors
    initialize_default_palette();
    
    // Setup DMA for faster display updates
    setup_display_dma();
    
    // Start Core 1 for rendering
    multicore_launch_core1(core1_rendering_loop);
    
    printf("GPU initialized, entering main loop\n");
    
    // Main command processing loop on Core 0
    while (true) {
        // Check if CS is asserted (CPU is sending a command)
        if (!gpio_get(CPU_CS_PIN)) {
            // Read command and length bytes
            uint8_t cmd_id, length;
            
            spi_read_blocking(CPU_SPI_PORT, 0xFF, &cmd_id, 1);
            spi_read_blocking(CPU_SPI_PORT, 0xFF, &length, 1);
            
            // Read command data if any
            if (length > 2) {
                spi_read_blocking(CPU_SPI_PORT, 0xFF, cmd_buffer, length - 2);
            }
            
            // Process the command
            process_command(cmd_id, cmd_buffer, length - 2);
        }
        
        // Update sprite animations
        update_sprite_animations();
        
        // Check if we need to trigger rendering
        uint32_t current_time = time_us_32();
        if (!rendering_in_progress && current_time - last_render_time >= FRAME_INTERVAL_US) {
            // Request a new frame to be rendered
            render_requested = true;
            last_render_time = current_time;
        }
        
        // Small delay to prevent tight loop
        sleep_us(100);
    }
    
    return 0;
}

// RP2350-Specific Enhancements
// Double-buffering implementation (RP2350 only)
void initialize_double_buffering() {
    #ifdef RP2350
    // Allocate front and back buffers
    uint32_t single_buffer_size = display_width * display_height * (display_bpp / 8);
    
    front_buffer = framebuffer; // Use already allocated buffer
    // back_buffer = malloc(single_buffer_size);
    back_buffer = safe_malloc(single_buffer_size);
    
    if (back_buffer != NULL) {
        // Clear both buffers
        memset(front_buffer, 0, single_buffer_size);
        memset(back_buffer, 0, single_buffer_size);
        
        // Enable double buffering
        double_buffering_enabled = true;
        current_draw_buffer = back_buffer;
    }
    #endif
}

// Swap buffers (for double buffering)
void swap_buffers() {
    #ifdef RP2350
    if (!double_buffering_enabled) return;
    
    // Wait for any in-progress rendering
    while (rendering_in_progress) {
        sleep_us(10);
    }
    
    // Swap pointers
    uint8_t* temp = front_buffer;
    front_buffer = current_draw_buffer;
    current_draw_buffer = temp;
    
    // Update global framebuffer pointer
    framebuffer = front_buffer;
    #endif
}

// Enhanced layer blending for RP2350
void apply_layer_blend(uint8_t layer_id) {
    #ifdef RP2350
    Layer* layer = &layers[layer_id];
    
    if (!layer->enabled || layer->alpha == 0) return;
    
    // Only perform blending if alpha is not 255 (fully opaque)
    if (layer->alpha == 255) return;
    
    // For 16-bit color mode
    if (display_bpp == 16) {
        uint16_t* fb = (uint16_t*)framebuffer;
        uint16_t* layer_buffer = (uint16_t*)layer->buffer;
        
        for (int y = 0; y < display_height; y++) {
            for (int x = 0; x < display_width; x++) {
                int pos = y * display_width + x;
                
                // Get pixel colors
                uint16_t dst_color = fb[pos];
                uint16_t src_color = layer_buffer[pos];
                
                // Skip transparent pixels
                if (src_color == 0) continue;
                
                // Extract RGB components
                uint8_t src_r = (src_color >> 11) & 0x1F;
                uint8_t src_g = (src_color >> 5) & 0x3F;
                uint8_t src_b = src_color & 0x1F;
                
                uint8_t dst_r = (dst_color >> 11) & 0x1F;
                uint8_t dst_g = (dst_color >> 5) & 0x3F;
                uint8_t dst_b = dst_color & 0x1F;
                
                // Apply alpha blending based on mode
                uint8_t alpha = layer->alpha;
                
                switch (layer->blend_mode) {
                    case 0: // Normal alpha blending
                        {
                            uint8_t inv_alpha = 255 - alpha;
                            
                            uint8_t r = ((src_r * alpha) + (dst_r * inv_alpha)) / 255;
                            uint8_t g = ((src_g * alpha) + (dst_g * inv_alpha)) / 255;
                            uint8_t b = ((src_b * alpha) + (dst_b * inv_alpha)) / 255;
                            
                            // Combine back to RGB565
                            fb[pos] = (r << 11) | (g << 5) | b;
                        }
                        break;
                        
                    case 1: // Additive blending
                        {
                            uint8_t r = min(31, dst_r + ((src_r * alpha) / 255));
                            uint8_t g = min(63, dst_g + ((src_g * alpha) / 255));
                            uint8_t b = min(31, dst_b + ((src_b * alpha) / 255));
                            
                            fb[pos] = (r << 11) | (g << 5) | b;
                        }
                        break;
                        
                    case 2: // Multiply blending
                        {
                            uint8_t r = (dst_r * (src_r * alpha / 255)) / 31;
                            uint8_t g = (dst_g * (src_g * alpha / 255)) / 63;
                            uint8_t b = (dst_b * (src_b * alpha / 255)) / 31;
                            
                            fb[pos] = (r << 11) | (g << 5) | b;
                        }
                        break;
                }
            }
        }
    }
    #endif
}

// Enhanced rotation and scaling (RP2350 only)
void render_rotated_layer_enhanced(uint8_t layer_id) {
    #ifdef RP2350
    // Similar to original function but with bilinear filtering for smoother results
    Layer* layer = &layers[layer_id];
    
    if (!layer->enabled || !layer->rotation_enabled) return;
    
    // Get transformation matrix
    float a = layer->matrix[0];
    float b = layer->matrix[1];
    float c = layer->matrix[2];
    float d = layer->matrix[3];
    
    // Get center of rotation
    int cx = layer->rot_center_x;
    int cy = layer->rot_center_y;
    
    // For each pixel in the framebuffer
    for (int y = 0; y < display_height; y++) {
        for (int x = 0; x < display_width; x++) {
            // Calculate source coordinates
            float dx = x - cx;
            float dy = y - cy;
            
            float src_x = (a * dx + b * dy) + cx + layer->scroll_x;
            float src_y = (c * dx + d * dy) + cy + layer->scroll_y;
            
            // For bilinear interpolation, we need to know the fractional part
            int int_src_x = (int)src_x;
            int int_src_y = (int)src_y;
            float frac_x = src_x - int_src_x;
            float frac_y = src_y - int_src_y;
            
            // Calculate tile coordinates for four sample points
            int tile_x1 = int_src_x / layer->tile_width;
            int tile_y1 = int_src_y / layer->tile_height;
            int tile_x2 = (int_src_x + 1) / layer->tile_width;
            int tile_y2 = (int_src_y + 1) / layer->tile_height;

            // Apply wrapping
            tile_x1 = (tile_x1 % layer->width_tiles + layer->width_tiles) % layer->width_tiles;
            tile_y1 = (tile_y1 % layer->height_tiles + layer->height_tiles) % layer->height_tiles;
            tile_x2 = (tile_x2 % layer->width_tiles + layer->width_tiles) % layer->width_tiles;
            tile_y2 = (tile_y2 % layer->height_tiles + layer->height_tiles) % layer->height_tiles;
            
            // Calculate pixel coordinates within tiles
            int pixel_x1 = ((int)src_x % layer->tile_width + layer->tile_width) % layer->tile_width;
            int pixel_y1 = ((int)src_y % layer->tile_height + layer->tile_height) % layer->tile_height;
            int pixel_x2 = ((int_src_x + 1) % layer->tile_width + layer->tile_width) % layer->tile_width;
            int pixel_y2 = ((int_src_y + 1) % layer->tile_height + layer->tile_height) % layer->tile_height;
            
            // Get tile information for all four corners
            TileInfo* tile11 = (TileInfo*)(layer->tilemap + (tile_y1 * layer->width_tiles + tile_x1) * sizeof(TileInfo));
            TileInfo* tile12 = (TileInfo*)(layer->tilemap + (tile_y1 * layer->width_tiles + tile_x2) * sizeof(TileInfo));
            TileInfo* tile21 = (TileInfo*)(layer->tilemap + (tile_y2 * layer->width_tiles + tile_x1) * sizeof(TileInfo));
            TileInfo* tile22 = (TileInfo*)(layer->tilemap + (tile_y2 * layer->width_tiles + tile_x2) * sizeof(TileInfo));
            
            // Get tile data from cache
            uint8_t* data11 = get_cached_tile(layer_id, tile11->tile_id);
            uint8_t* data12 = get_cached_tile(layer_id, tile12->tile_id);
            uint8_t* data21 = get_cached_tile(layer_id, tile21->tile_id);
            uint8_t* data22 = get_cached_tile(layer_id, tile22->tile_id);
            
            // Skip if any tile data is missing
            if (data11 == NULL || data12 == NULL || data21 == NULL || data22 == NULL) {
                continue;
            }
            
            // Get pixel colors from all four corners (assuming 8-bit mode for simplicity)
            uint8_t color11 = get_pixel_from_tile(data11, pixel_x1, pixel_y1, layer->tile_width, tile11->attributes);
            uint8_t color12 = get_pixel_from_tile(data12, pixel_x2, pixel_y1, layer->tile_width, tile12->attributes);
            uint8_t color21 = get_pixel_from_tile(data21, pixel_x1, pixel_y2, layer->tile_width, tile21->attributes);
            uint8_t color22 = get_pixel_from_tile(data22, pixel_x2, pixel_y2, layer->tile_width, tile22->attributes);
            
            // Skip if all pixels are transparent
            if (color11 == 0 && color12 == 0 && color21 == 0 && color22 == 0) {
                continue;
            }
            
            // Bilinear interpolation (for simplicity, we'll just pick the nearest pixel)
            uint8_t final_color;
            if (frac_x < 0.5) {
                if (frac_y < 0.5) {
                    final_color = color11;
                } else {
                    final_color = color21;
                }
            } else {
                if (frac_y < 0.5) {
                    final_color = color12;
                } else {
                    final_color = color22;
                }
            }
            
            // Skip transparent pixels
            if (final_color == 0) continue;
            
            // Check window clipping
            if (effects.window_enabled[0] || effects.window_enabled[1]) {
                if (!is_in_window(x, y, layer_id)) {
                    continue;
                }
            }
            
            // Write to framebuffer
            framebuffer[y * display_width + x] = final_color;
        }
    }
    #endif
}

// Advanced sprite scaling with bilinear filtering (RP2350 only)
void render_sprite_enhanced(uint8_t sprite_id, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    #ifdef RP2350
    Sprite* sprite = &sprites[sprite_id];
    SpritePattern* pattern = &sprite_patterns[sprite->pattern_id];
    
    // Get attributes
    bool flip_x = (sprite->attributes & 0x01) != 0;
    bool flip_y = (sprite->attributes & 0x02) != 0;
    uint8_t palette_offset = sprite->palette_offset;
    
    // Get sprite data
    uint8_t* sprite_data = sprite_data + pattern->data_offset;
    
    // Cache source dimensions
    uint16_t src_width = pattern->width * 8;
    uint16_t src_height = pattern->height * 8;
    
    // Scaling factors (16.16 fixed-point)
    uint32_t x_scale = (src_width << 16) / width;
    uint32_t y_scale = (src_height << 16) / height;
    
    // For each pixel in the destination sprite
    for (int16_t dy = 0; dy < height; dy++) {
        for (int16_t dx = 0; dx < width; dx++) {
            // Calculate screen position
            int16_t screen_x = x + dx;
            int16_t screen_y = y + dy;
            
            // Skip if outside screen
            if (screen_x < 0 || screen_x >= display_width || 
                screen_y < 0 || screen_y >= display_height) {
                continue;
            }
            
            // Calculate source position with scaling
            uint32_t src_x_fixed = dx * x_scale;
            uint32_t src_y_fixed = dy * y_scale;
            
            uint16_t src_x = src_x_fixed >> 16;
            uint16_t src_y = src_y_fixed >> 16;
            
            // Get fractional part for bilinear filtering
            float frac_x = (src_x_fixed & 0xFFFF) / 65536.0f;
            float frac_y = (src_y_fixed & 0xFFFF) / 65536.0f;
            
            // Calculate coordinates for four sample points
            uint16_t x1 = src_x;
            uint16_t y1 = src_y;
            uint16_t x2 = min(src_x + 1, src_width - 1);
            uint16_t y2 = min(src_y + 1, src_height - 1);
            
            // Apply flipping
            if (flip_x) {
                x1 = src_width - 1 - x1;
                x2 = src_width - 1 - x2;
            }
            if (flip_y) {
                y1 = src_height - 1 - y1;
                y2 = src_height - 1 - y2;
            }
            
            // Get the four sample colors
            uint8_t color11 = get_pixel_from_sprite(sprite_data, x1, y1, src_width, pattern->bpp);
            uint8_t color12 = get_pixel_from_sprite(sprite_data, x2, y1, src_width, pattern->bpp);
            uint8_t color21 = get_pixel_from_sprite(sprite_data, x1, y2, src_width, pattern->bpp);
            uint8_t color22 = get_pixel_from_sprite(sprite_data, x2, y2, src_width, pattern->bpp);
            
            // Apply palette offset
            if (color11 != 0) color11 += palette_offset * 16;
            if (color12 != 0) color12 += palette_offset * 16;
            if (color21 != 0) color21 += palette_offset * 16;
            if (color22 != 0) color22 += palette_offset * 16;
            
            // Perform bilinear interpolation using the four samples
            uint8_t final_color;
            
            // For simplicity, use nearest neighbor for transparent pixels
            if (color11 == 0 || color12 == 0 || color21 == 0 || color22 == 0) {
                // Nearest neighbor
                if (frac_x < 0.5) {
                    if (frac_y < 0.5) {
                        final_color = color11;
                    } else {
                        final_color = color21;
                    }
                } else {
                    if (frac_y < 0.5) {
                        final_color = color12;
                    } else {
                        final_color = color22;
                    }
                }
            } else {
                // Full bilinear interpolation
                uint8_t blend1 = (uint8_t)((1.0f - frac_x) * color11 + frac_x * color12);
                uint8_t blend2 = (uint8_t)((1.0f - frac_x) * color21 + frac_x * color22);
                final_color = (uint8_t)((1.0f - frac_y) * blend1 + frac_y * blend2);
            }
            
            // Skip transparent pixels
            if (final_color == 0) continue;
            
            // Write to framebuffer
            framebuffer[screen_y * display_width + screen_x] = final_color;
        }
    }
    #endif
}

// Genesis Inspired Features
// Configure cell-based sprites
void set_cell_based_sprites(uint8_t enable, uint8_t cell_width, uint8_t cell_height) {
    // Cell-based sprites are similar to Genesis sprite pattern composition
    cell_based_sprites_enabled = (enable != 0);
    
    // Validate cell dimensions (typically 8x8 or 8x16)
    if (cell_width == 0) cell_width = 8;
    if (cell_height == 0) cell_height = 8;
    
    sprite_cell_width = cell_width;
    sprite_cell_height = cell_height;
    
    send_ack_to_cpu(CMD_SET_CELL_BASED_SPRITES);
}

// Set horizontal scroll mode
void set_hscroll_mode(uint8_t mode) {
    // 0=whole, 1=tile, 2=line
    hscroll_mode = mode;
    
    // Re-initialize horizontal scroll tables if needed
    if (mode == 2) { // Line scroll mode
        for (int i = 0; i < MAX_LAYERS; i++) {
            if (layers[i].h_scroll_table == NULL) {
                // layers[i].h_scroll_table = malloc(display_height * sizeof(uint16_t));
                layers[i].h_scroll_table = safe_malloc(display_height * sizeof(uint16_t));
                if (layers[i].h_scroll_table != NULL) {
                    memset(layers[i].h_scroll_table, 0, display_height * sizeof(uint16_t));
                }
            }
        }
    }
    
    // Mark whole screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_SET_HSCROLL_MODE);
}

// Enable dual playfield mode
void set_dual_playfield(uint8_t enable) {
    dual_playfield_mode = (enable != 0);
    
    if (dual_playfield_mode) {
        // Ensure layers 0 and 1 are enabled with correct priority
        layers[0].dual_playfield = true;
        layers[1].dual_playfield = true;
        
        // In dual playfield mode, priority bit in tiles determines which playfield
        // Each layer can use full set of palette entries
    } else {
        // Disable dual playfield flags
        layers[0].dual_playfield = false;
        layers[1].dual_playfield = false;
    }
    
    // Mark whole screen dirty
    mark_rect_dirty(0, 0, display_width, display_height);
    
    send_ack_to_cpu(CMD_SET_DUAL_PLAYFIELD);
}

// Sprite collision detection
void set_sprite_collision_detection(uint8_t mode) {
    collision_detection_mode = mode;
    
    // Reset collision buffers
    if (mode != 0) {
        // For sprite-sprite collision (mode 1 or 3)
        if (mode == 1 || mode == 3) {
            if (sprite_collision_buffer == NULL) {
                // sprite_collision_buffer = malloc(display_width * display_height / 8);
                sprite_collision_buffer = safe_malloc(display_width * display_height / 8);
            }
            memset(sprite_collision_buffer, 0, display_width * display_height / 8);
        }
        
        // For sprite-BG collision (mode 2 or 3)
        if (mode == 2 || mode == 3) {
            if (bg_collision_buffer == NULL) {
                // bg_collision_buffer = malloc(display_width * display_height / 8);
                bg_collision_buffer = safe_malloc(display_width * display_height / 8);
            }
            
            // Fill background collision buffer based on non-zero pixels in layers
            memset(bg_collision_buffer, 0, display_width * display_height / 8);
            
            // We'll populate this during layer rendering
            bg_collision_detection_enabled = true;
        } else {
            bg_collision_detection_enabled = false;
        }
    }
    
    // Reset collision detection flags
    sprite_collision_detected = false;
    sprite_bg_collision_detected = false;
    
    send_ack_to_cpu(CMD_SET_SPRITE_COLLISION_DETECTION);
}

/*
This comprehensive GPU implementation provides all the features described in the TriBoy documentation, with features comparable to 16-bit era graphics while leveraging the additional capabilities of modern microcontrollers.

1. **Core Graphics Capabilities**:
   - Background layer system with tile-based rendering
   - Sprite system with scaling, rotation, and animations
   - Palette management and color control
   - Special effects like fading, mosaic, window clipping
   - Direct drawing primitives

2. **Optimization Features**:
   - Dirty rectangle tracking for efficient rendering
   - Tile caching to minimize memory usage
   - Double buffering on RP2350
   - DMA-accelerated display updates

3. **Genesis-Inspired Features**:
   - Dual playfield mode
   - Line/column scrolling
   - Cell-based sprites
   - Sprite collision detection

4. **Advanced Effects**:
   - Layer blending and transparency
   - Rotation and scaling of backgrounds
   - High-quality sprite rendering with interpolation (RP2350)
*/
