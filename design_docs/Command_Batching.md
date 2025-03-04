# Implementing Command Batching for TriBoy's Inter-Microcontroller Communication
Command batching significantly improves performance by reducing SPI transaction overhead. Rather than sending many small commands, we can group related commands into larger batches, reducing latency and improving throughput.

## CPU-Side Command Batching Implementation
Batching system in the CPU's command queue:

```c
// Add to cpu.c

// Batch command structure
typedef struct {
    uint8_t batch_command_id;  // Command ID for the entire batch
    uint8_t command_count;     // Number of commands in batch
    uint8_t device_id;         // 1=GPU, 2=APU
    uint16_t total_data_size;  // Total size of all commands in batch
    uint8_t* data_buffer;      // Buffer for batched command data
    uint16_t current_offset;   // Current offset into data buffer
} CommandBatch;

// Active command batches (one per device)
CommandBatch gpu_batch = {0};
CommandBatch apu_batch = {0};

// Batch command IDs
#define CMD_BATCH_SPRITES      0xB0
#define CMD_BATCH_TILES        0xB1
#define CMD_BATCH_LAYERS       0xB2
#define CMD_BATCH_DRAW         0xB3
#define CMD_BATCH_AUDIO        0xB4
#define CMD_BATCH_CHANNELS     0xB5

// Initialize command batching system
void init_command_batching() {
    // Allocate batch buffers
    gpu_batch.data_buffer = malloc(MAX_BATCH_SIZE);
    apu_batch.data_buffer = malloc(MAX_BATCH_SIZE);
    
    gpu_batch.device_id = 1;
    apu_batch.device_id = 2;
    
    // Clear batch state
    reset_command_batch(&gpu_batch);
    reset_command_batch(&apu_batch);
}

// Reset a command batch
void reset_command_batch(CommandBatch* batch) {
    batch->batch_command_id = 0;
    batch->command_count = 0;
    batch->total_data_size = 0;
    batch->current_offset = 0;
}

// Start a new command batch
bool start_command_batch(CommandBatch* batch, uint8_t batch_type) {
    // Check if a batch is already in progress
    if (batch->command_count > 0) {
        flush_command_batch(batch);
    }
    
    // Initialize the new batch
    batch->batch_command_id = batch_type;
    batch->command_count = 0;
    batch->total_data_size = 0;
    batch->current_offset = 0;
    
    return true;
}

// Add a command to the current batch
bool add_to_command_batch(CommandBatch* batch, uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    // Check if we have enough space in the batch
    if (batch->current_offset + length + 2 > MAX_BATCH_SIZE) {
        // Batch is full, flush it first
        flush_command_batch(batch);
        
        // Reinitialize batch with the same type
        uint8_t batch_type = batch->batch_command_id;
        start_command_batch(batch, batch_type);
    }
    
    // Add command to batch
    batch->data_buffer[batch->current_offset++] = cmd_id;
    batch->data_buffer[batch->current_offset++] = length;
    
    if (length > 2) {
        memcpy(&batch->data_buffer[batch->current_offset], data, length - 2);
        batch->current_offset += (length - 2);
    }
    
    batch->command_count++;
    batch->total_data_size += length;
    
    return true;
}

// Flush a command batch to the device
void flush_command_batch(CommandBatch* batch) {
    // Skip if batch is empty
    if (batch->command_count == 0) return;
    
    // Prepare batch header
    uint8_t header[4];
    header[0] = batch->batch_command_id;
    header[1] = batch->command_count;
    header[2] = (batch->total_data_size >> 8) & 0xFF;  // Size high byte
    header[3] = batch->total_data_size & 0xFF;         // Size low byte
    
    // Send batch to device
    if (batch->device_id == 1) {
        // GPU batch
        send_gpu_data(header, 4, batch->data_buffer, batch->current_offset);
    } else {
        // APU batch
        send_apu_data(header, 4, batch->data_buffer, batch->current_offset);
    }
    
    // Reset batch state
    reset_command_batch(batch);
}

// Send data to GPU with combined header and data
void send_gpu_data(uint8_t* header, uint8_t header_size, uint8_t* data, uint16_t data_size) {
    mutex_enter_blocking(&gpu_queue.lock);
    
    gpio_put(GPU_CS_PIN, 0);  // Select GPU
    
    // Send header
    spi_write_blocking(GPU_SPI_PORT, header, header_size);
    
    // Send data
    spi_write_blocking(GPU_SPI_PORT, data, data_size);
    
    gpio_put(GPU_CS_PIN, 1);  // Deselect GPU
    
    mutex_exit(&gpu_queue.lock);
}

// Send data to APU with combined header and data
void send_apu_data(uint8_t* header, uint8_t header_size, uint8_t* data, uint16_t data_size) {
    mutex_enter_blocking(&apu_queue.lock);
    
    gpio_put(APU_CS_PIN, 0);  // Select APU
    
    // Send header
    spi_write_blocking(APU_SPI_PORT, header, header_size);
    
    // Send data
    spi_write_blocking(APU_SPI_PORT, data, data_size);
    
    gpio_put(APU_CS_PIN, 1);  // Deselect APU
    
    mutex_exit(&apu_queue.lock);
}
```

## Optimized API Functions for Command Batching
Create optimized versions of the API functions that use batching:

```c
// Batched sprite operations
void begin_sprite_batch() {
    start_command_batch(&gpu_batch, CMD_BATCH_SPRITES);
}

void batch_move_sprite(uint8_t sprite_id, int16_t x, int16_t y) {
    uint8_t data[5];
    data[0] = sprite_id;
    data[1] = (x >> 8) & 0xFF;
    data[2] = x & 0xFF;
    data[3] = (y >> 8) & 0xFF;
    data[4] = y & 0xFF;
    
    add_to_command_batch(&gpu_batch, 0x42, 6, data);
}

void batch_set_sprite_attributes(uint8_t sprite_id, uint8_t attributes, uint8_t palette_offset) {
    uint8_t data[3];
    data[0] = sprite_id;
    data[1] = attributes;
    data[2] = palette_offset;
    
    add_to_command_batch(&gpu_batch, 0x43, 4, data);
}

void batch_animate_sprite(uint8_t sprite_id, uint8_t start_frame, uint8_t end_frame, 
                         uint8_t frame_rate, uint8_t loop_mode) {
    uint8_t data[5];
    data[0] = sprite_id;
    data[1] = start_frame;
    data[2] = end_frame;
    data[3] = frame_rate;
    data[4] = loop_mode;
    
    add_to_command_batch(&gpu_batch, 0x46, 6, data);
}

void end_sprite_batch() {
    flush_command_batch(&gpu_batch);
}

// Batched layer operations
void begin_layer_batch() {
    start_command_batch(&gpu_batch, CMD_BATCH_LAYERS);
}

void batch_scroll_layer(uint8_t layer_id, int16_t x, int16_t y) {
    uint8_t data[5];
    data[0] = layer_id;
    data[1] = (x >> 8) & 0xFF;
    data[2] = x & 0xFF;
    data[3] = (y >> 8) & 0xFF;
    data[4] = y & 0xFF;
    
    add_to_command_batch(&gpu_batch, 0x23, 6, data);
}

void end_layer_batch() {
    flush_command_batch(&gpu_batch);
}

// Batched audio channel operations
void begin_audio_channel_batch() {
    start_command_batch(&apu_batch, CMD_BATCH_CHANNELS);
}

void batch_set_channel_volume(uint8_t channel_id, uint8_t volume) {
    uint8_t data[2];
    data[0] = channel_id;
    data[1] = volume;
    
    add_to_command_batch(&apu_batch, 0x30, 3, data);
}

void batch_set_channel_pan(uint8_t channel_id, uint8_t pan) {
    uint8_t data[2];
    data[0] = channel_id;
    data[1] = pan;
    
    add_to_command_batch(&apu_batch, 0x31, 3, data);
}

void end_audio_channel_batch() {
    flush_command_batch(&apu_batch);
}
```

## Receiver Side: GPU and APU Batch Command Processing
Implement the receiver side in the GPU and APU to handle these batched commands:

```c
// Add to gpu.c

// Process a batch of commands
void process_command_batch(uint8_t batch_type, uint8_t command_count, 
                          uint16_t total_size, const uint8_t* data) {
    uint16_t current_offset = 0;
    
    // Process each command in the batch
    for (int i = 0; i < command_count; i++) {
        // Extract command info
        uint8_t cmd_id = data[current_offset++];
        uint8_t cmd_len = data[current_offset++];
        
        // Process this command
        process_command(cmd_id, &data[current_offset], cmd_len - 2);
        
        // Advance to next command
        current_offset += (cmd_len - 2);
    }
    
    // Send a single acknowledgment for the entire batch
    send_ack_to_cpu(batch_type);
}

// Update main command processor to handle batch commands
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    // Check for batch commands
    if (cmd_id >= 0xB0 && cmd_id <= 0xB5) {
        // This is a batch command
        uint8_t command_count = data[0];
        uint16_t total_size = (data[1] << 8) | data[2];
        
        // Process the batch
        process_command_batch(cmd_id, command_count, total_size, &data[3]);
        return;
    }
    
    // Process regular commands as before
    switch (cmd_id) {
        // Original command processing code
        // ...
    }
}
```

The APU implementation would be similar, but adapted for its specific command set. Left out for brevity.

## Integration in the Main Game Loop
Modify the main game loop to utilize these batch commands:

```c
// Update in cpu.c

void update_game() {
    // Process game logic here...
    
    // Prepare rendering with batched commands
    prepare_rendering_batched();
    
    // Prepare audio with batched commands
    prepare_audio_batched();
    
    // Rest of update_game function...
}

void prepare_rendering_batched() {
    // Update sprites in batch
    begin_sprite_batch();
    
    // Process player sprite
    batch_move_sprite(PLAYER_SPRITE_ID, player_x, player_y);
    
    if (player_is_animated) {
        batch_animate_sprite(PLAYER_SPRITE_ID, player_anim_start, 
                           player_anim_end, player_anim_speed, 1);
    }
    
    // Process enemy sprites
    for (int i = 0; i < active_enemy_count; i++) {
        Enemy* enemy = &enemies[i];
        batch_move_sprite(enemy->sprite_id, enemy->x, enemy->y);
        
        if (enemy->is_animated) {
            batch_animate_sprite(enemy->sprite_id, enemy->anim_start, 
                               enemy->anim_end, enemy->anim_speed, 1);
        }
    }
    
    // Process projectile sprites
    for (int i = 0; i < active_projectile_count; i++) {
        Projectile* proj = &projectiles[i];
        batch_move_sprite(proj->sprite_id, proj->x, proj->y);
    }
    
    // End sprite batch (sends to GPU)
    end_sprite_batch();
    
    // Update background layers in batch
    begin_layer_batch();
    
    // Scroll background layers
    batch_scroll_layer(0, background_scroll_x, 0);
    batch_scroll_layer(1, background_scroll_x / 2, 0); // Parallax effect
    
    // End layer batch (sends to GPU)
    end_layer_batch();
    
    // Additional rendering preparation if needed
    // ...
}

void prepare_audio_batched() {
    // Update audio channels in batch
    begin_audio_channel_batch();
    
    // Process music channels (volume based on distance)
    for (int i = 0; i < MUSIC_CHANNEL_COUNT; i++) {
        batch_set_channel_volume(i, music_volumes[i]);
    }
    
    // Process sound effect channels
    for (int i = 0; i < active_sound_effects; i++) {
        SoundEffect* sfx = &sound_effects[i];
        batch_set_channel_volume(sfx->channel_id, sfx->volume);
        batch_set_channel_pan(sfx->channel_id, sfx->pan);
    }
    
    // End audio channel batch (sends to APU)
    end_audio_channel_batch();
    
    // Additional audio preparation if needed
    // ...
}
```

## Specialized Batch Types for Common Operations
Add specialized batching for common operations:

```c
// Add to cpu.c

// Sprite visibility batch (show/hide multiple sprites at once)
void batch_sprite_visibility(uint8_t* sprite_ids, bool* visibility, uint8_t count) {
    begin_sprite_batch();
    
    for (int i = 0; i < count; i++) {
        // Use show or hide command based on visibility
        if (visibility[i]) {
            add_to_command_batch(&gpu_batch, 0x45, 2, &sprite_ids[i]); // Show sprite
        } else {
            add_to_command_batch(&gpu_batch, 0x44, 2, &sprite_ids[i]); // Hide sprite
        }
    }
    
    end_sprite_batch();
}

// Palette update batch
void batch_update_palette(uint8_t start_index, RGB* colors, uint8_t count) {
    // Start a palette batch
    start_command_batch(&gpu_batch, CMD_BATCH_TILES);
    
    // Prepare palette data
    uint8_t data[256];
    data[0] = start_index;
    data[1] = count;
    
    // Copy RGB values
    for (int i = 0; i < count; i++) {
        data[2 + i*3] = colors[i].r;
        data[3 + i*3] = colors[i].g;
        data[4 + i*3] = colors[i].b;
    }
    
    // Add to batch
    add_to_command_batch(&gpu_batch, 0x11, 2 + count*3, data);
    
    // Flush the batch
    flush_command_batch(&gpu_batch);
}

// Direct drawing batch for UI elements
void begin_ui_draw_batch() {
    start_command_batch(&gpu_batch, CMD_BATCH_DRAW);
}

void batch_draw_rect(int16_t x, int16_t y, uint16_t width, uint16_t height, 
                    uint8_t color, bool fill) {
    uint8_t data[9];
    data[0] = (x >> 8) & 0xFF;
    data[1] = x & 0xFF;
    data[2] = (y >> 8) & 0xFF;
    data[3] = y & 0xFF;
    data[4] = (width >> 8) & 0xFF;
    data[5] = width & 0xFF;
    data[6] = (height >> 8) & 0xFF;
    data[7] = height & 0xFF;
    data[8] = fill ? 1 : 0;
    
    add_to_command_batch(&gpu_batch, 0x82, 10, data);
}

void end_ui_draw_batch() {
    flush_command_batch(&gpu_batch);
}
```

## Game-Specific Optimized Batch Functions
Add some game-specific batched functions for common scenarios:

```c
// Player and enemy update batch for a top-down game
void batch_update_entities() {
    begin_sprite_batch();
    
    // Update player sprite
    batch_move_sprite(0, player.x, player.y);
    batch_set_sprite_attributes(0, get_player_direction_attr(), 0);
    
    // Update all enemies in one batch
    for (int i = 0; i < enemy_count; i++) {
        uint8_t sprite_id = i + 1; // Enemies start at sprite ID 1
        batch_move_sprite(sprite_id, enemies[i].x, enemies[i].y);
        batch_set_sprite_attributes(sprite_id, get_enemy_direction_attr(i), 0);
        
        // Set enemy animation based on state
        if (enemies[i].state == ENEMY_WALKING) {
            batch_animate_sprite(sprite_id, 0, 3, 8, 1); // Walk animation
        } else if (enemies[i].state == ENEMY_ATTACKING) {
            batch_animate_sprite(sprite_id, 4, 7, 12, 0); // Attack animation (play once)
        }
    }
    
    end_sprite_batch();
}

// Platformer-specific scrolling function
void batch_platformer_scroll(int16_t camera_x, int16_t camera_y) {
    begin_layer_batch();
    
    // Parallax scrolling for background layers
    batch_scroll_layer(0, camera_x / 4, camera_y / 4);  // Far background (slowest)
    batch_scroll_layer(1, camera_x / 2, camera_y / 2);  // Mid background
    batch_scroll_layer(2, camera_x, camera_y);          // Main level layer
    batch_scroll_layer(3, camera_x * 2, 0);            // Foreground details (fastest)
    
    end_layer_batch();
}

// Update multiple audio channels for game environment
void batch_update_game_audio(int16_t player_x, int16_t player_y) {
    begin_audio_channel_batch();
    
    // Main background music channels (first 4 channels)
    for (int i = 0; i < 4; i++) {
        batch_set_channel_volume(i, game_music_volume);
    }
    
    // Environment sounds with position-based panning
    for (int i = 0; i < ambient_sound_count; i++) {
        AmbientSound* sound = &ambient_sounds[i];
        
        // Calculate distance from player to sound source
        int16_t dx = sound->x - player_x;
        int16_t dy = sound->y - player_y;
        float distance = sqrtf(dx*dx + dy*dy);
        
        // Calculate volume based on distance (fade with distance)
        uint8_t volume = distance < sound->max_distance ? 
                       255 * (1.0f - distance / sound->max_distance) : 0;
        
        // Calculate panning based on horizontal position relative to player
        uint8_t pan = dx < 0 ? (128 - min(128, abs(dx)/2)) : (128 + min(127, dx/2));
        
        // Set channel parameters
        batch_set_channel_volume(sound->channel_id, volume);
        batch_set_channel_pan(sound->channel_id, pan);
    }
    
    end_audio_channel_batch();
}
```

## Performance Optimization: Automatic Batching
Add a system that automatically batches commands in the background:

```c
// Command queuing with automatic batching
typedef enum {
    BATCH_AUTO = 0,
    BATCH_SPRITE,
    BATCH_LAYER,
    BATCH_DRAW,
    BATCH_AUDIO
} BatchCategory;

// Get batch category for a command
BatchCategory get_command_batch_category(uint8_t cmd_id) {
    if (cmd_id >= 0x40 && cmd_id <= 0x4F) return BATCH_SPRITE;     // Sprite commands
    if (cmd_id >= 0x20 && cmd_id <= 0x2F) return BATCH_LAYER;      // Layer commands
    if (cmd_id >= 0x80 && cmd_id <= 0x8F) return BATCH_DRAW;       // Drawing commands
    if (cmd_id >= 0x30 && cmd_id <= 0x3F) return BATCH_AUDIO;      // Audio channel commands
    return BATCH_AUTO;                                            // No specific category
}

// Queue command with automatic batching
bool queue_command_auto_batch(uint8_t device_id, uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    CommandBatch* batch = (device_id == 1) ? &gpu_batch : &apu_batch;
    
    // Get batch category for this command
    BatchCategory category = get_command_batch_category(cmd_id);
    
    // If different category than current batch, flush and start new
    if (batch->command_count > 0) {
        uint8_t current_batch_type = batch->batch_command_id;
        BatchCategory current_category;
        
        switch (current_batch_type) {
            case CMD_BATCH_SPRITES: current_category = BATCH_SPRITE; break;
            case CMD_BATCH_LAYERS: current_category = BATCH_LAYER; break;
            case CMD_BATCH_DRAW: current_category = BATCH_DRAW; break;
            case CMD_BATCH_CHANNELS: current_category = BATCH_AUDIO; break;
            default: current_category = BATCH_AUTO; break;
        }
        
        if (category != current_category) {
            // Need to switch batch type
            flush_command_batch(batch);
        }
    }
    
    // If no active batch, start one
    if (batch->command_count == 0) {
        uint8_t batch_type;
        
        switch (category) {
            case BATCH_SPRITE: batch_type = CMD_BATCH_SPRITES; break;
            case BATCH_LAYER: batch_type = CMD_BATCH_LAYERS; break;
            case BATCH_DRAW: batch_type = CMD_BATCH_DRAW; break;
            case BATCH_AUDIO: batch_type = CMD_BATCH_CHANNELS; break;
            default: batch_type = device_id == 1 ? CMD_BATCH_SPRITES : CMD_BATCH_CHANNELS; break;
        }
        
        start_command_batch(batch, batch_type);
    }
    
    // Add command to current batch
    return add_to_command_batch(batch, cmd_id, length, data);
}

// Replace original GPU/APU command functions with auto-batching versions
bool queue_gpu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    return queue_command_auto_batch(1, cmd_id, length, data);
}

bool queue_apu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    return queue_command_auto_batch(2, cmd_id, length, data);
}

// Update end of frame processing to flush any pending batches
void end_frame_processing() {
    // Flush any pending batched commands
    flush_command_batch(&gpu_batch);
    flush_command_batch(&apu_batch);
    
    // Wait for vsync
    wait_for_vsync();
}
```

## Integration into the Run Game Loop
Integrate the full batching system into the main game loop:

```c
// Updated main game loop with comprehensive batching
void run_game_loop() {
    // Initialize batching
    init_command_batching();
    
    // Main game loop
    while (game_state.game_active) {
        uint32_t frame_start = time_us_32();
        
        // Process input
        process_input();
        
        // Update game state
        update_game_state();
        
        // ===== SPRITE UPDATES =====
        begin_sprite_batch();
        
        // Update player sprite
        update_player_sprite();
        
        // Update enemy sprites
        update_enemy_sprites();
        
        // Update projectile sprites
        update_projectile_sprites();
        
        // Update effect sprites
        update_effect_sprites();
        
        // End sprite batch
        end_sprite_batch();
        
        // ===== BACKGROUND UPDATES =====
        begin_layer_batch();
        
        // Update main level scroll
        batch_scroll_layer(0, camera_x, camera_y);
        
        // Update parallax backgrounds
        update_parallax_backgrounds();
        
        // End layer batch
        end_layer_batch();
        
        // ===== UI DRAWING =====
        if (ui_needs_update) {
            begin_ui_draw_batch();
            draw_ui_elements();
            end_ui_draw_batch();
            ui_needs_update = false;
        }
        
        // ===== AUDIO UPDATES =====
        begin_audio_channel_batch();
        
        // Update music channels
        update_music_channels();
        
        // Update sound effect channels
        update_sound_effects();
        
        // End audio batch
        end_audio_channel_batch();
        
        // Process any automated batch flushes
        end_frame_processing();
        
        // Frame rate control
        uint32_t frame_time = time_us_32() - frame_start;
        if (frame_time < TARGET_FRAME_TIME) {
            sleep_us(TARGET_FRAME_TIME - frame_time);
        }
    }
}
```

## Benchmarking and Performance Analysis
Simple benchmarking system to measure the performance improvements:

```c
// Add to cpu.c

// Benchmarking variables
uint32_t cmd_timing_start;
uint32_t cmd_timing_end;
uint32_t unbatched_total_time = 0;
uint32_t batched_total_time = 0;
uint32_t unbatched_cmd_count = 0;
uint32_t batched_cmd_count = 0;

// Benchmark individual commands vs batched commands
void benchmark_sprite_updates() {
    const int SPRITE_COUNT = 50;
    int16_t positions[SPRITE_COUNT][2];
    
    // Generate random sprite positions
    for (int i = 0; i < SPRITE_COUNT; i++) {
        positions[i][0] = rand() % display_width;
        positions[i][1] = rand() % display_height;
    }
    
    // Benchmark unbatched commands
    cmd_timing_start = time_us_32();
    
    for (int i = 0; i < SPRITE_COUNT; i++) {
        uint8_t data[5];
        data[0] = i;
        data[1] = (positions[i][0] >> 8) & 0xFF;
        data[2] = positions[i][0] & 0xFF;
        data[3] = (positions[i][1] >> 8) & 0xFF;
        data[4] = positions[i][1] & 0xFF;
        
        // Use direct SPI transfer for accurate timing
        gpio_put(GPU_CS_PIN, 0);
        spi_write_blocking(GPU_SPI_PORT, "\x42\x06", 2); // Command ID and length
        spi_write_blocking(GPU_SPI_PORT, data, 5);
        gpio_put(GPU_CS_PIN, 1);
    }
    
    cmd_timing_end = time_us_32();
    unbatched_total_time += (cmd_timing_end - cmd_timing_start);
    unbatched_cmd_count += SPRITE_COUNT;
    
    // Benchmark batched commands
    cmd_timing_start = time_us_32();
    
    begin_sprite_batch();
    for (int i = 0; i < SPRITE_COUNT; i++) {
        batch_move_sprite(i, positions[i][0], positions[i][1]);
    }
    end_sprite_batch();
    
    cmd_timing_end = time_us_32();
    batched_total_time += (cmd_timing_end - cmd_timing_start);
    batched_cmd_count += SPRITE_COUNT;
    
    // Print results
    printf("Unbatched: %lu us for %lu commands (%.2f us/cmd)\n", 
           unbatched_total_time, unbatched_cmd_count, 
           (float)unbatched_total_time / unbatched_cmd_count);
    
    printf("Batched: %lu us for %lu commands (%.2f us/cmd)\n", 
           batched_total_time, batched_cmd_count, 
           (float)batched_total_time / batched_cmd_count);
    
    printf("Speedup: %.2fx\n", 
           (float)unbatched_total_time / batched_total_time);
}

// Record and analyze frame timing with batching enabled/disabled
void analyze_frame_timing(bool use_batching) {
    const int FRAME_COUNT = 100;
    uint32_t frame_times[FRAME_COUNT];
    
    // Enable/disable batching
    batching_enabled = use_batching;
    
    // Run game for FRAME_COUNT frames and measure timing
    for (int i = 0; i < FRAME_COUNT; i++) {
        uint32_t frame_start = time_us_32();
        
        // Process one frame
        process_input();
        update_game_state();
        prepare_rendering();
        prepare_audio();
        end_frame_processing();
        
        // Record frame time
        frame_times[i] = time_us_32() - frame_start;
        
        // Wait for vsync
        wait_for_vsync();
    }
    
    // Calculate statistics
    uint32_t total_time = 0;
    uint32_t min_time = UINT32_MAX;
    uint32_t max_time = 0;
    
    for (int i = 0; i < FRAME_COUNT; i++) {
        total_time += frame_times[i];
        min_time = min(min_time, frame_times[i]);
        max_time = max(max_time, frame_times[i]);
    }
    
    float avg_time = (float)total_time / FRAME_COUNT;
    
    // Calculate variance and standard deviation
    float variance = 0;
    for (int i = 0; i < FRAME_COUNT; i++) {
        float diff = frame_times[i] - avg_time;
        variance += diff * diff;
    }
    variance /= FRAME_COUNT;
    float std_dev = sqrtf(variance);
    
    // Print results
    printf("Frame timing (%s batching):\n", use_batching ? "with" : "without");
    printf("  Average: %.2f us (%.2f fps)\n", avg_time, 1000000.0f / avg_time);
    printf("  Min: %lu us, Max: %lu us\n", min_time, max_time);
    printf("  Standard deviation: %.2f us\n", std_dev);
}
```