# Software Architecture

## Boot Process
```c
void boot_sequence() {
    // Initialize hardware
    initialize_hardware();
    
    // Mount SD card
    if (!mount_sd_card()) {
        display_error("SD Card Error");
        return;
    }
    
    // Load system configuration
    load_system_config();
    
    // Initialize GPU
    initialize_gpu();
    
    // Initialize APU
    initialize_apu();
    
    // Load boot screen assets to GPU
    load_boot_assets();
    
    // Display boot screen
    display_boot_screen();
    
    // Initialize game selection menu
    initialize_game_menu();
    
    // Wait for user to select game
    GameInfo selected_game = run_game_selector();
    
    // Load selected game
    load_game(selected_game);
}
```

## Game Loading Process
```c
bool load_game(GameInfo game) {
    // Open game file on SD card
    FILE* game_file = open_game_file(game.filename);
    if (!game_file) return false;
    
    // Read game header
    GameHeader header;
    read_game_header(game_file, &header);
    
    // Allocate memory for game code
    void* game_code_memory = allocate_game_memory(header.code_size);
    
    // Load game code into memory
    load_game_code(game_file, game_code_memory, header.code_size);
    
    // Load GPU assets into GPU QSPI memory
    load_gpu_assets(game_file, &header.gpu_assets);
    
    // Load APU assets into APU QSPI memory
    load_apu_assets(game_file, &header.apu_assets);
    
    // Initialize game state
    initialize_game_state(&header);
    
    // Close file to free SD resources
    close_file(game_file);
    
    // Start the game
    start_game(game_code_memory);
    
    return true;
}
```

## Asset Management System
```c
typedef struct {
    uint32_t id;
    AssetType type;
    uint32_t size;
    uint32_t offset;
    bool loaded;
    uint8_t target; // 0=CPU, 1=GPU, 2=APU
} AssetInfo;

// Load asset from SD card to target device
bool load_asset(AssetInfo* asset) {
    // Open the assets file
    FILE* asset_file = open_asset_file();
    if (!asset_file) return false;
    
    // Seek to asset position
    seek_file(asset_file, asset->offset);
    
    // Allocate buffer for the asset
    uint8_t* buffer = allocate_asset_buffer(asset->size);
    if (!buffer) {
        close_file(asset_file);
        return false;
    }
    
    // Read asset into buffer
    read_file(asset_file, buffer, asset->size);
    
    // Close file to free SD resources
    close_file(asset_file);
    
    // Send asset to appropriate device
    switch (asset->target) {
        case 0: // CPU
            copy_to_cpu_memory(buffer, asset->id, asset->size);
            break;
            
        case 1: // GPU
            send_to_gpu(buffer, asset->id, asset->type, asset->size);
            break;
            
        case 2: // APU
            send_to_apu(buffer, asset->id, asset->type, asset->size);
            break;
    }
    
    // Free buffer
    free_asset_buffer(buffer);
    
    asset->loaded = true;
    return true;
}
```

## GPU Communication System
```c
typedef struct {
    uint8_t command_id;
    uint8_t length;
    uint8_t data[256];
} GPUCommand;

typedef struct {
    GPUCommand commands[GPU_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    mutex_t lock;
} GPUCommandQueue;

GPUCommandQueue gpu_queue;

// Add command to GPU queue
bool queue_gpu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    // Acquire lock for thread safety
    mutex_enter_blocking(&gpu_queue.lock);
    
    // Check if queue is full
    if (gpu_queue.count >= GPU_QUEUE_SIZE) {
        mutex_exit(&gpu_queue.lock);
        return false;
    }
    
    // Add command to queue
    GPUCommand* cmd = &gpu_queue.commands[gpu_queue.tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    memcpy(cmd->data, data, length - 2); // Subtract 2 for command_id and length
    
    // Update queue pointers
    gpu_queue.tail = (gpu_queue.tail + 1) % GPU_QUEUE_SIZE;
    gpu_queue.count++;
    
    // Release lock
    mutex_exit(&gpu_queue.lock);
    
    return true;
}

// Process GPU commands from queue
void process_gpu_queue() {
    // Process up to N commands per call to avoid blocking too long
    const int MAX_PROCESS = 10;
    int processed = 0;
    
    while (processed < MAX_PROCESS) {
        // Acquire lock
        mutex_enter_blocking(&gpu_queue.lock);
        
        // Check if queue is empty
        if (gpu_queue.count == 0) {
            mutex_exit(&gpu_queue.lock);
            break;
        }
        
        // Get command from queue
        GPUCommand* cmd = &gpu_queue.commands[gpu_queue.head];
        
        // Prepare buffer for SPI transfer
        uint8_t buffer[258];
        buffer[0] = cmd->command_id;
        buffer[1] = cmd->length;
        memcpy(&buffer[2], cmd->data, cmd->length - 2);
        
        // Release lock before communication
        gpu_queue.head = (gpu_queue.head + 1) % GPU_QUEUE_SIZE;
        gpu_queue.count--;
        mutex_exit(&gpu_queue.lock);
        
        // Send command to GPU via SPI
        gpio_put(GPU_CS_PIN, 0);
        spi_write_blocking(SPI_PORT, buffer, cmd->length);
        gpio_put(GPU_CS_PIN, 1);
        
        processed++;
        
        // Wait for GPU to process command if needed
        if (is_blocking_command(cmd->command_id)) {
            wait_for_gpu_ready();
        }
    }
}
```

## APU Communication System
```c
typedef struct {
    uint8_t command_id;
    uint8_t length;
    uint8_t data[256];
} APUCommand;

typedef struct {
    APUCommand commands[APU_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    mutex_t lock;
} APUCommandQueue;

APUCommandQueue apu_queue;

// Add command to APU queue
bool queue_apu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    // Implementation similar to GPU queue
    // (code omitted for brevity)
}

// Process APU commands from queue
void process_apu_queue() {
    // Implementation similar to GPU queue processing
    // (code omitted for brevity)
}

// Load sample data to APU
bool load_sample_to_apu(uint8_t sample_id, const uint8_t* data, 
                      uint16_t size, uint8_t format, uint16_t sample_rate) {
    // Prepare SAMPLE_LOAD command (0x70)
    uint8_t cmd_data[256];
    cmd_data[0] = sample_id;
    cmd_data[1] = format;
    cmd_data[2] = sample_rate & 0xFF;
    cmd_data[3] = (sample_rate >> 8) & 0xFF;
    cmd_data[4] = 0; // loop_start low byte
    cmd_data[5] = 0; // loop_start high byte
    cmd_data[6] = 0; // loop_end low byte
    cmd_data[7] = 0; // loop_end high byte
    cmd_data[8] = size & 0xFF;
    cmd_data[9] = (size >> 8) & 0xFF;
    
    // We need to send the command header first
    queue_apu_command(0x70, 12, cmd_data);
    
    // Then send the sample data in chunks
    const uint16_t CHUNK_SIZE = 256;
    uint16_t remaining = size;
    uint16_t offset = 0;
    
    while (remaining > 0) {
        uint16_t chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        
        // Create custom data transfer command
        queue_apu_command(0xF0, chunk + 2, &data[offset]);
        
        offset += chunk;
        remaining -= chunk;
    }
    
    return true;
}
```

## Inter-core Communication

```c
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

// Inter-core queues
queue_t core0_to_core1_queue;
queue_t core1_to_core0_queue;

// Core 1 main function
void core1_main() {
    // Initialize Core 1
    initialize_core1();
    
    CoreMessage msg;
    
    while (true) {
        // Check for messages from Core 0
        if (queue_try_remove(&core0_to_core1_queue, &msg)) {
            switch (msg.type) {
                case MSG_LOAD_ASSET:
                    // Load asset from SD card
                    load_asset((AssetInfo*)msg.data);
                    break;
                    
                case MSG_PROCESS_GPU_QUEUE:
                    // Process GPU command queue
                    process_gpu_queue();
                    break;
                    
                case MSG_PROCESS_APU_QUEUE:
                    // Process APU command queue
                    process_apu_queue();
                    break;
                    
                case MSG_GAME_EVENT:
                    // Handle game-specific events
                    handle_game_event(msg.param1, msg.param2);
                    break;
            }
        }
        
        // Perform background tasks
        perform_background_tasks();
        
        // Check if new assets need to be loaded
        check_asset_loading_needs();
        
        // Ensure command queues are processed regularly
        if (gpu_queue.count > 0) {
            process_gpu_queue();
        }
        
        if (apu_queue.count > 0) {
            process_apu_queue();
        }
    }
}
```

## SD Card and QSPI Memory Management

```c
// File system instance
FATFS fs;

// Initialize SD card
bool initialize_sd_card() {
    // Configure SPI for SD card
    spi_init(SD_SPI_PORT, 25000000);
    gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_SCK_PIN, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so initialize high
    gpio_init(SD_CS_PIN);
    gpio_set_dir(SD_CS_PIN, GPIO_OUT);
    gpio_put(SD_CS_PIN, 1);
    
    // Mount the file system
    FRESULT fr = f_mount(&fs, "", 1);
    return (fr == FR_OK);
}

// Send data to GPU QSPI memory
bool send_to_gpu_qspi(uint32_t qspi_address, const uint8_t* data, uint32_t size) {
    // Split into manageable chunks
    const uint16_t CHUNK_SIZE = 1024;
    uint32_t remaining = size;
    uint32_t offset = 0;
    
    // Send QSPI memory write command to GPU
    uint8_t cmd_header[8];
    cmd_header[0] = 0xE0; // GPU QSPI write command
    cmd_header[1] = 8;    // Command length
    cmd_header[2] = (qspi_address >> 24) & 0xFF;
    cmd_header[3] = (qspi_address >> 16) & 0xFF;
    cmd_header[4] = (qspi_address >> 8) & 0xFF;
    cmd_header[5] = qspi_address & 0xFF;
    cmd_header[6] = (size >> 8) & 0xFF;
    cmd_header[7] = size & 0xFF;
    
    // Send command header
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(SPI_PORT, cmd_header, 8);
    gpio_put(GPU_CS_PIN, 1);
    
    // Wait for GPU to be ready
    wait_for_gpu_ready();
    
    // Send data in chunks
    while (remaining > 0) {
        uint16_t chunk = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        
        gpio_put(GPU_CS_PIN, 0);
        spi_write_blocking(SPI_PORT, &data[offset], chunk);
        gpio_put(GPU_CS_PIN, 1);
        
        offset += chunk;
        remaining -= chunk;
        
        // Wait for GPU to process this chunk
        wait_for_gpu_ready();
    }
    
    return true;
}

// Send data to APU QSPI memory
bool send_to_apu_qspi(uint32_t qspi_address, const uint8_t* data, uint32_t size) {
    // Similar to GPU QSPI function but for APU
    // (code omitted for brevity)
}
```

## Game Loop Implementation

```c
// Main game loop function
void run_game_loop() {
    // Get start time for frame timing
    uint32_t frame_start = time_us_32();
    
    // Process input
    process_input();
    
    // Update game state
    update_game_state();
    
    // Check for asset loading requests
    check_asset_requirements();
    
    // Prepare rendering commands
    prepare_render_commands();
    
    // Send message to Core 1 to process GPU commands
    CoreMessage gpu_msg = {MSG_PROCESS_GPU_QUEUE, 0, 0, NULL};
    queue_add_blocking(&core0_to_core1_queue, &gpu_msg);
    
    // Prepare audio commands
    prepare_audio_commands();
    
    // Send message to Core 1 to process APU commands
    CoreMessage apu_msg = {MSG_PROCESS_APU_QUEUE, 0, 0, NULL};
    queue_add_blocking(&core0_to_core1_queue, &apu_msg);
    
    // Calculate time spent on frame
    uint32_t frame_time = time_us_32() - frame_start;
    
    // Delay to maintain target frame rate (e.g., 60 FPS)
    uint32_t target_frame_time = 16667; // 1/60 sec in microseconds
    if (frame_time < target_frame_time) {
        sleep_us(target_frame_time - frame_time);
    }
}
```

## High-Level APIs for Game Developers

```c
// Simple sprite management
void set_sprite(uint8_t sprite_id, uint8_t pattern_id, int16_t x, int16_t y, uint8_t attributes) {
    uint8_t data[9];
    data[0] = sprite_id;
    data[1] = pattern_id;
    data[2] = (x >> 8) & 0xFF;
    data[3] = x & 0xFF;
    data[4] = (y >> 8) & 0xFF;
    data[5] = y & 0xFF;
    data[6] = attributes;
    data[7] = 0; // palette offset
    data[8] = 128; // scale (1.0)
    
    queue_gpu_command(0x41, 10, data);
}

// Background layer scrolling
void scroll_background(uint8_t layer_id, int16_t x, int16_t y) {
    uint8_t data[5];
    data[0] = layer_id;
    data[1] = (x >> 8) & 0xFF;
    data[2] = x & 0xFF;
    data[3] = (y >> 8) & 0xFF;
    data[4] = y & 0xFF;
    
    queue_gpu_command(0x23, 6, data);
}

// Play sound effect
void play_sound_effect(uint8_t channel, uint8_t sample_id, uint8_t pitch, uint8_t volume) {
    uint8_t data[4];
    data[0] = channel;
    data[1] = sample_id;
    data[2] = pitch;
    data[3] = volume;
    
    queue_apu_command(0x71, 5, data);
}

// Start music track
void play_music(uint8_t tracker_id) {
    uint8_t data[1];
    data[0] = tracker_id;
    
    queue_apu_command(0x11, 2, data);
}
```
