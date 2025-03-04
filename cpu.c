/*
The CPU is built around an RP2040 (264KB RAM) or RP2350 (520KB RAM) microcontroller with the following memory allocation:
  - **RP2040 (264KB)**: 96KB game code and state, 64KB asset management buffers, 48KB command queues (24KB GPU, 24KB APU), 56KB system variables and stack
  - **RP2350 (520KB)**: 240KB game code and state, 128KB asset management buffers, 96KB command queues (48KB GPU, 48KB APU), 56KB system variables and stack

The CPU serves as the master controller with these key responsibilities:
  1. Running game logic and main loop
  2. Managing assets and resource loading
  3. Processing user input
  4. Coordinating with GPU and APU via command queues
  5. Handling SD card storage for game data

Implementation Approach:
  1. Efficiently manages the two cores (game logic on Core 0, system management on Core 1)
  2. Provides robust asset loading and management
  3. Implements efficient command queues for GPU and APU
  4. Creates a clean game loop with proper timing
  5. Handles SD card I/O and asset caching

---

Core allocation:
  Core 0: Game logic, main loop, input processing
  Core 1: Asset management, GPU/APU communication, SD card I/O

Memory regions:
  - Game code/state: Main game data and variables (96KB/240KB)
  - Asset buffers: Cached assets from SD card (64KB/128KB)
  - Command queues: Communication with GPU and APU (48KB/96KB)
  - System variables: Core system state and stack (56KB)

Architecture:
  - CPU is the master controller, coordinating GPU and APU
  - Communication via SPI with CPU as master
  - Asset loading managed primarily through SD card
  - Game loop synchronized with GPU via VSYNC
*/

// Hardware Initialization and System Setup
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

// Button pins
#define BTN_UP_PIN 6
#define BTN_DOWN_PIN 7
#define BTN_LEFT_PIN 8
#define BTN_RIGHT_PIN 9
#define BTN_A_PIN 16
#define BTN_B_PIN 17
#define BTN_START_PIN 26
#define BTN_SELECT_PIN 27

// Initialize hardware
void init_hardware() {
    // Initialize stdio for debugging
    stdio_init_all();
    printf("TriBoy CPU Initializing...\n");
    
    // Initialize SPI for GPU communication
    spi_init(GPU_SPI_PORT, 20000000); // 20MHz
    gpio_set_function(GPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(GPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(GPU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(GPU_CS_PIN);
    gpio_set_dir(GPU_CS_PIN, GPIO_OUT);
    gpio_put(GPU_CS_PIN, 1); // Deselect
    
    // Initialize SPI for APU communication
    spi_init(APU_SPI_PORT, 20000000); // 20MHz
    gpio_set_function(APU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(APU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(APU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(APU_CS_PIN);
    gpio_set_dir(APU_CS_PIN, GPIO_OUT);
    gpio_put(APU_CS_PIN, 1); // Deselect
    
    // Initialize SPI for SD card
    spi_init(spi_default, 12500000); // 12.5MHz for SD card
    gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_SCK_PIN, GPIO_FUNC_SPI);
    
    gpio_init(SD_CS_PIN);
    gpio_set_dir(SD_CS_PIN, GPIO_OUT);
    gpio_put(SD_CS_PIN, 1); // Deselect
    
    // Initialize VSYNC interrupt from GPU
    gpio_init(VSYNC_PIN);
    gpio_set_dir(VSYNC_PIN, GPIO_IN);
    gpio_pull_up(VSYNC_PIN);
    
    // Initialize buttons
    gpio_init(BTN_UP_PIN);
    gpio_init(BTN_DOWN_PIN);
    gpio_init(BTN_LEFT_PIN);
    gpio_init(BTN_RIGHT_PIN);
    gpio_init(BTN_A_PIN);
    gpio_init(BTN_B_PIN);
    gpio_init(BTN_START_PIN);
    gpio_init(BTN_SELECT_PIN);
    
    // Set button pins as inputs with pull-ups
    gpio_set_dir(BTN_UP_PIN, GPIO_IN);
    gpio_set_dir(BTN_DOWN_PIN, GPIO_IN);
    gpio_set_dir(BTN_LEFT_PIN, GPIO_IN);
    gpio_set_dir(BTN_RIGHT_PIN, GPIO_IN);
    gpio_set_dir(BTN_A_PIN, GPIO_IN);
    gpio_set_dir(BTN_B_PIN, GPIO_IN);
    gpio_set_dir(BTN_START_PIN, GPIO_IN);
    gpio_set_dir(BTN_SELECT_PIN, GPIO_IN);
    
    gpio_pull_up(BTN_UP_PIN);
    gpio_pull_up(BTN_DOWN_PIN);
    gpio_pull_up(BTN_LEFT_PIN);
    gpio_pull_up(BTN_RIGHT_PIN);
    gpio_pull_up(BTN_A_PIN);
    gpio_pull_up(BTN_B_PIN);
    gpio_pull_up(BTN_START_PIN);
    gpio_pull_up(BTN_SELECT_PIN);
    
    printf("Hardware initialization complete\n");
}

// Clock Synchronization Implementation
// Global timing variables
volatile uint32_t global_frame_counter = 0;
volatile uint64_t master_clock_timestamp = 0;
const uint32_t SYNC_INTERVAL_MS = 1000; // Sync every second
uint32_t last_sync_time = 0;

void init_clock_synchronization() {
    // Initialize master clock timestamp at boot
    master_clock_timestamp = time_us_64();
    last_sync_time = time_ms_32();

    // Setup periodic sync timer using hardware timer
    hardware_timer_init(TIMER_SYNC, SYNC_INTERVAL_MS, send_clock_sync);
}

void send_clock_sync() {
    // Update master timestamp
    master_clock_timestamp = time_us_64();

    // Send clock sync to GPU
    uint8_t gpu_sync_cmd[10] = {
        0xF1,                             // Clock sync command ID
        10,                               // Command length
        (global_frame_counter >> 24) & 0xFF, // Frame counter bytes
        (global_frame_counter >> 16) & 0xFF,
        (global_frame_counter >> 8) & 0xFF,
        global_frame_counter & 0xFF,
        (master_clock_timestamp >> 32) & 0xFF, // Timestamp bytes (high)
        (master_clock_timestamp >> 24) & 0xFF,
        (master_clock_timestamp >> 16) & 0xFF,
        (master_clock_timestamp >> 8) & 0xFF
    };

    // Critical section - ensure atomic access to SPI
    mutex_enter_blocking(&spi_mutex);

    // Send to GPU with high priority
    gpio_put(GPU_CS_PIN, 0);
    spi_write_blocking(GPU_SPI_PORT, gpu_sync_cmd, 10);
    // Wait for acknowledgment
    uint8_t ack = 0;
    spi_read_blocking(GPU_SPI_PORT, 0xFF, &ack, 1);
    gpio_put(GPU_CS_PIN, 1);

    // Send to APU with high priority
    gpio_put(APU_CS_PIN, 0);
    spi_write_blocking(APU_SPI_PORT, gpu_sync_cmd, 10);
    // Wait for acknowledgment
    spi_read_blocking(APU_SPI_PORT, 0xFF, &ack, 1);
    gpio_put(APU_CS_PIN, 1);

    mutex_exit(&spi_mutex);

    // Log sync for debugging
    if (debug_enabled) {
        printf("Clock sync sent: frame=%lu timestamp=%llu\n",
               global_frame_counter, master_clock_timestamp);
    }
}

// Called every frame in the main game loop
void update_frame_timing() {
    // Increment frame counter
    global_frame_counter++;

    // Trigger periodic clock sync if needed
    uint32_t current_time = time_ms_32();
    if (current_time - last_sync_time >= SYNC_INTERVAL_MS) {
        send_clock_sync();
        last_sync_time = current_time;
    }
}

// Command Queue Management
// Command structure for GPU and APU
typedef struct {
    uint8_t command_id;
    uint8_t length;
    uint8_t data[256]; // Maximum command data size
} Command;

// Command queue structure
typedef struct {
    Command* commands;
    uint16_t capacity;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    mutex_t lock;
} CommandQueue;

// Global command queues
CommandQueue gpu_queue;
CommandQueue apu_queue;

// Initialize command queues
void init_command_queues() {
    // Determine if we're on RP2040 or RP2350
    bool is_rp2350 = check_if_rp2350();
    
    // Allocate GPU command queue
    gpu_queue.capacity = is_rp2350 ? 256 : 128;
    gpu_queue.commands = malloc(sizeof(Command) * gpu_queue.capacity);
    gpu_queue.head = 0;
    gpu_queue.tail = 0;
    gpu_queue.count = 0;
    mutex_init(&gpu_queue.lock);
    
    // Allocate APU command queue
    apu_queue.capacity = is_rp2350 ? 256 : 128;
    apu_queue.commands = malloc(sizeof(Command) * apu_queue.capacity);
    apu_queue.head = 0;
    apu_queue.tail = 0;
    apu_queue.count = 0;
    mutex_init(&apu_queue.lock);
    
    printf("Command queues initialized\n");
}

// Add a command to the GPU queue
bool queue_gpu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    mutex_enter_blocking(&gpu_queue.lock);
    
    // Check if queue is full
    if (gpu_queue.count >= gpu_queue.capacity) {
        mutex_exit(&gpu_queue.lock);
        return false;
    }
    
    // Add command to queue
    Command* cmd = &gpu_queue.commands[gpu_queue.tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    
    // Copy command data
    if (length > 2) {
        memcpy(cmd->data, data, length - 2);
    }
    
    // Update queue
    gpu_queue.tail = (gpu_queue.tail + 1) % gpu_queue.capacity;
    gpu_queue.count++;
    
    mutex_exit(&gpu_queue.lock);
    return true;
}

// Add a command to the APU queue
bool queue_apu_command(uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    mutex_enter_blocking(&apu_queue.lock);
    
    // Check if queue is full
    if (apu_queue.count >= apu_queue.capacity) {
        mutex_exit(&apu_queue.lock);
        return false;
    }
    
    // Add command to queue
    Command* cmd = &apu_queue.commands[apu_queue.tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    
    // Copy command data
    if (length > 2) {
        memcpy(cmd->data, data, length - 2);
    }
    
    // Update queue
    apu_queue.tail = (apu_queue.tail + 1) % apu_queue.capacity;
    apu_queue.count++;
    
    mutex_exit(&apu_queue.lock);
    return true;
}

// Process commands from the GPU queue
void process_gpu_queue() {
    const int MAX_BATCH = 10; // Process up to 10 commands at once
    int processed = 0;
    
    while (processed < MAX_BATCH) {
        mutex_enter_blocking(&gpu_queue.lock);
        
        // Check if queue is empty
        if (gpu_queue.count == 0) {
            mutex_exit(&gpu_queue.lock);
            break;
        }
        
        // Get command from queue
        Command* cmd = &gpu_queue.commands[gpu_queue.head];
        
        // Prepare buffer for SPI transfer
        uint8_t buffer[258]; // Maximum command size
        buffer[0] = cmd->command_id;
        buffer[1] = cmd->length;
        
        if (cmd->length > 2) {
            memcpy(&buffer[2], cmd->data, cmd->length - 2);
        }
        
        // Update queue pointers
        gpu_queue.head = (gpu_queue.head + 1) % gpu_queue.capacity;
        gpu_queue.count--;
        
        // Release lock before SPI communication
        mutex_exit(&gpu_queue.lock);
        
        // Send command to GPU
        gpio_put(GPU_CS_PIN, 0); // Select GPU
        spi_write_blocking(GPU_SPI_PORT, buffer, cmd->length);
        gpio_put(GPU_CS_PIN, 1); // Deselect GPU
        
        processed++;
        
        // Brief delay between commands
        sleep_us(10);
    }
}

// Process commands from the APU queue
void process_apu_queue() {
    const int MAX_BATCH = 10; // Process up to 10 commands at once
    int processed = 0;
    
    while (processed < MAX_BATCH) {
        mutex_enter_blocking(&apu_queue.lock);
        
        // Check if queue is empty
        if (apu_queue.count == 0) {
            mutex_exit(&apu_queue.lock);
            break;
        }
        
        // Get command from queue
        Command* cmd = &apu_queue.commands[apu_queue.head];
        
        // Prepare buffer for SPI transfer
        uint8_t buffer[258]; // Maximum command size
        buffer[0] = cmd->command_id;
        buffer[1] = cmd->length;
        
        if (cmd->length > 2) {
            memcpy(&buffer[2], cmd->data, cmd->length - 2);
        }
        
        // Update queue pointers
        apu_queue.head = (apu_queue.head + 1) % apu_queue.capacity;
        apu_queue.count--;
        
        // Release lock before SPI communication
        mutex_exit(&apu_queue.lock);
        
        // Send command to APU
        gpio_put(APU_CS_PIN, 0); // Select APU
        spi_write_blocking(APU_SPI_PORT, buffer, cmd->length);
        gpio_put(APU_CS_PIN, 1); // Deselect APU
        
        processed++;
        
        // Brief delay between commands
        sleep_us(10);
    }
}

// CPU Reception of Acknowledgments

void process_ack_packet(uint8_t device_id, uint8_t* packet) {
    // Extract original command ID and status
    uint8_t cmd_id = packet[2];
    uint8_t status = packet[3];

    // Get appropriate command queue
    EnhancedCommandQueue* queue = (device_id == 1) ? &gpu_queue : &apu_queue;

    mutex_enter_blocking(&queue->lock);

    // Find the command in the queue
    int found = 0;
    uint16_t index = queue->head;
    for (uint16_t i = 0; i < queue->count; i++) {
        EnhancedCommand* cmd = &queue->commands[index];
        if (cmd->command_id == cmd_id && cmd->requires_ack && !cmd->completed) {
            // Mark command as completed
            cmd->completed = true;
            found = 1;

            if (debug_enabled) {
                printf("Received ACK for command 0x%02X from device %d\n",
                      cmd_id, device_id);
            }

            break;
        }
        index = (index + 1) % queue->capacity;
    }

    mutex_exit(&queue->lock);

    if (!found && debug_enabled) {
        printf("Received ACK for unknown command 0x%02X\n", cmd_id);
    }
}

void check_for_device_responses() {
    // Check GPU data ready pin
    if (gpio_get(GPU_DATA_READY_PIN)) {
        // Read response from GPU
        uint8_t response[4];

        gpio_put(GPU_CS_PIN, 0);
        spi_read_blocking(GPU_SPI_PORT, 0xFF, response, 4);
        gpio_put(GPU_CS_PIN, 1);

        // Process based on response type
        if (response[0] == 0xFA) {
            // Acknowledgment
            process_ack_packet(1, response);
        } else if (response[0] == 0xFE) {
            // Error
            process_error_packet(1, response);
        }
    }

    // Check APU data ready pin
    if (gpio_get(APU_DATA_READY_PIN)) {
        // Read response from APU
        uint8_t response[4];

        gpio_put(APU_CS_PIN, 0);
        spi_read_blocking(APU_SPI_PORT, 0xFF, response, 4);
        gpio_put(APU_CS_PIN, 1);

        // Process based on response type
        if (response[0] == 0xFA) {
            // Acknowledgment
            process_ack_packet(2, response);
        } else if (response[0] == 0xFE) {
            // Error
            process_error_packet(2, response);
        }
    }
}

// CPU Error Recovery System

typedef enum {
    ERR_NONE = 0,
    ERR_TIMEOUT = 1,
    ERR_INVALID_COMMAND = 2,
    ERR_MEMORY_FULL = 3,
    ERR_INVALID_PARAMETER = 4,
    ERR_DEVICE_BUSY = 5,
    ERR_COMMUNICATION_FAILURE = 6,
    ERR_SYNC_LOST = 7
} ErrorCode;

typedef struct {
    uint8_t device_id;    // 1=GPU, 2=APU
    ErrorCode error_code;
    uint8_t command_id;
    uint32_t timestamp;
    bool handled;
} ErrorRecord;

#define ERROR_LOG_SIZE 32
ErrorRecord error_log[ERROR_LOG_SIZE];
uint8_t error_log_index = 0;
uint8_t unhandled_errors = 0;

void log_error(uint8_t device_id, ErrorCode code, uint8_t cmd_id) {
    // Add to error log
    ErrorRecord* record = &error_log[error_log_index];
    record->device_id = device_id;
    record->error_code = code;
    record->command_id = cmd_id;
    record->timestamp = time_ms_32();
    record->handled = false;

    // Increment counters
    error_log_index = (error_log_index + 1) % ERROR_LOG_SIZE;
    unhandled_errors++;

    // Log error for debugging
    if (debug_enabled) {
        const char* device = (device_id == 1) ? "GPU" : "APU";
        printf("ERROR: %s cmd 0x%02X error code %d\n", device, cmd_id, code);
    }
}

void process_error_packet(uint8_t device_id, uint8_t* packet) {
    uint8_t cmd_id = packet[2];
    uint8_t error_code = packet[3];

    // Log the error
    log_error(device_id, error_code, cmd_id);

    // Handle specific errors
    handle_device_error(device_id, error_code, cmd_id);
}

void handle_device_error(uint8_t device_id, ErrorCode error_code, uint8_t cmd_id) {
    switch (error_code) {
        case ERR_MEMORY_FULL:
            // Device ran out of memory - try recovery
            if (device_id == 1) {
                // GPU memory issue - try to free resources
                queue_gpu_command(0xD0, 2, NULL); // Request memory cleanup
            } else {
                // APU memory issue
                queue_apu_command(0xD0, 2, NULL); // Request memory cleanup
            }
            break;

        case ERR_SYNC_LOST:
            // Synchronization lost - initiate resync
            send_clock_sync();
            break;

        case ERR_DEVICE_BUSY:
            // Device is busy, retry later
            sleep_ms(5); // Small delay before retrying
            break;

        case ERR_COMMUNICATION_FAILURE:
            // Serious communication issue - try resetting the interface
            reset_communication_interface(device_id);
            break;

        default:
            // Log but don't take specific action for other errors
            if (debug_enabled) {
                printf("Unhandled error %d from device %d\n", error_code, device_id);
            }
            break;
    }
}

void reset_communication_interface(uint8_t device_id) {
    if (device_id == 1) {
        // Reset GPU communication
        spi_deinit(GPU_SPI_PORT);
        sleep_ms(10);
        spi_init(GPU_SPI_PORT, 20000000); // 20MHz
        gpio_set_function(GPU_SCK_PIN, GPIO_FUNC_SPI);
        gpio_set_function(GPU_MOSI_PIN, GPIO_FUNC_SPI);
        gpio_set_function(GPU_MISO_PIN, GPIO_FUNC_SPI);

        // Send reset signal
        gpio_put(GPU_RESET_PIN, 0);
        sleep_ms(10);
        gpio_put(GPU_RESET_PIN, 1);
        sleep_ms(50); // Give time for GPU to reset

        // Re-initialize GPU
        initialize_gpu();
    } else {
        // Reset APU communication (similar pattern)
        spi_deinit(APU_SPI_PORT);
        sleep_ms(10);
        spi_init(APU_SPI_PORT, 20000000);
        gpio_set_function(APU_SCK_PIN, GPIO_FUNC_SPI);
        gpio_set_function(APU_MOSI_PIN, GPIO_FUNC_SPI);
        gpio_set_function(APU_MISO_PIN, GPIO_FUNC_SPI);

        gpio_put(APU_RESET_PIN, 0);
        sleep_ms(10);
        gpio_put(APU_RESET_PIN, 1);
        sleep_ms(50);

        initialize_apu();
    }

    // Log recovery attempt
    if (debug_enabled) {
        printf("Communication interface for device %d reset\n", device_id);
    }
}

bool check_device_health(uint8_t device_id) {
    // Send ping command
    uint8_t ping_cmd[2] = {0xF0, 2}; // NOP command
    uint8_t response = 0;
    bool success = false;

    if (device_id == 1) {
        gpio_put(GPU_CS_PIN, 0);
        spi_write_blocking(GPU_SPI_PORT, ping_cmd, 2);
        spi_read_blocking(GPU_SPI_PORT, 0xFF, &response, 1);
        gpio_put(GPU_CS_PIN, 1);
    } else {
        gpio_put(APU_CS_PIN, 0);
        spi_write_blocking(APU_SPI_PORT, ping_cmd, 2);
        spi_read_blocking(APU_SPI_PORT, 0xFF, &response, 1);
        gpio_put(APU_CS_PIN, 1);
    }

    // Response should be 0xAA for healthy device
    success = (response == 0xAA);

    if (!success && debug_enabled) {
        printf("Device %d health check failed\n", device_id);
    }

    return success;
}

// Improved Command Queue Error Handling
bool queue_command(CommandQueue* queue, uint8_t cmd_id, uint8_t length, const uint8_t* data) {
    mutex_enter_blocking(&queue->lock);
    if (queue->count >= queue->capacity) {
        mutex_exit(&queue->lock);
        send_error_to_cpu(ERROR_QUEUE_FULL);
        return false;
    }
    Command* cmd = &queue->commands[queue->tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    memcpy(cmd->data, data, length - 2);
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    mutex_exit(&queue->lock);
    return true;
}

// Asset Management System
// Asset types
typedef enum {
    ASSET_TYPE_TILESET,
    ASSET_TYPE_TILEMAP,
    ASSET_TYPE_SPRITE,
    ASSET_TYPE_PALETTE,
    ASSET_TYPE_SAMPLE,
    ASSET_TYPE_MUSIC,
    ASSET_TYPE_FONT,
    ASSET_TYPE_LEVEL
} AssetType;

// Asset information structure
typedef struct {
    uint32_t id;
    AssetType type;
    uint32_t size;
    uint32_t offset;  // Offset in asset file
    bool loaded;
    uint8_t target;   // 0=CPU, 1=GPU, 2=APU
    char name[32];    // Asset name for debugging
} AssetInfo;

// Asset cache entry
typedef struct {
    uint32_t asset_id;
    uint8_t* data;
    uint32_t size;
    uint32_t last_used;  // For LRU replacement
} AssetCacheEntry;

// Global asset state
#define MAX_ASSETS 256
#define MAX_CACHED_ASSETS 32

AssetInfo assets[MAX_ASSETS];
uint32_t asset_count = 0;

AssetCacheEntry asset_cache[MAX_CACHED_ASSETS];
uint32_t asset_cache_count = 0;
uint32_t frame_counter = 0;

// SD card file handle
FIL asset_file;
bool asset_file_open = false;

// Initialize asset system
void init_asset_system() {
    // Clear asset registry
    memset(assets, 0, sizeof(assets));
    asset_count = 0;
    
    // Clear asset cache
    for (int i = 0; i < MAX_CACHED_ASSETS; i++) {
        asset_cache[i].asset_id = 0;
        asset_cache[i].data = NULL;
        asset_cache[i].size = 0;
        asset_cache[i].last_used = 0;
    }
    asset_cache_count = 0;
    
    printf("Asset system initialized\n");
}

// Load asset registry from a file
bool load_asset_registry(const char* filename) {
    FIL f;
    FRESULT fr;
    
    // Open asset registry file
    fr = f_open(&f, filename, FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open asset registry: %s\n", filename);
        return false;
    }
    
    // Read asset count
    UINT br;
    fr = f_read(&f, &asset_count, sizeof(uint32_t), &br);
    if (fr != FR_OK || br != sizeof(uint32_t)) {
        f_close(&f);
        return false;
    }
    
    // Limit to maximum assets
    if (asset_count > MAX_ASSETS) {
        asset_count = MAX_ASSETS;
    }
    
    // Read asset info entries
    fr = f_read(&f, assets, sizeof(AssetInfo) * asset_count, &br);
    if (fr != FR_OK || br != sizeof(AssetInfo) * asset_count) {
        f_close(&f);
        return false;
    }
    
    // Close file
    f_close(&f);
    
    // Reset loaded state for all assets
    for (uint32_t i = 0; i < asset_count; i++) {
        assets[i].loaded = false;
    }
    
    printf("Loaded asset registry: %lu assets\n", asset_count);
    return true;
}

// Open the main asset data file
bool open_asset_file(const char* filename) {
    FRESULT fr;
    
    // Close previous file if open
    if (asset_file_open) {
        f_close(&asset_file);
        asset_file_open = false;
    }
    
    // Open asset data file
    fr = f_open(&asset_file, filename, FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open asset file: %s\n", filename);
        return false;
    }
    
    asset_file_open = true;
    return true;
}

// Find asset by ID
AssetInfo* find_asset(uint32_t asset_id) {
    for (uint32_t i = 0; i < asset_count; i++) {
        if (assets[i].id == asset_id) {
            return &assets[i];
        }
    }
    return NULL;
}

// Find asset in cache
uint8_t* find_asset_in_cache(uint32_t asset_id, uint32_t* size) {
    for (uint32_t i = 0; i < asset_cache_count; i++) {
        if (asset_cache[i].asset_id == asset_id) {
            asset_cache[i].last_used = frame_counter;
            if (size != NULL) {
                *size = asset_cache[i].size;
            }
            return asset_cache[i].data;
        }
    }
    return NULL;
}

// Add asset to cache
void cache_asset(uint32_t asset_id, uint8_t* data, uint32_t size) {
    // Check if already in cache
    for (uint32_t i = 0; i < asset_cache_count; i++) {
        if (asset_cache[i].asset_id == asset_id) {
            // Already cached, update
            if (asset_cache[i].size != size) {
                // Resize buffer
                free(asset_cache[i].data);
                asset_cache[i].data = malloc(size);
                asset_cache[i].size = size;
            }
            memcpy(asset_cache[i].data, data, size);
            asset_cache[i].last_used = frame_counter;
            return;
        }
    }
    
    // Not in cache, find a slot
    uint32_t slot;
    
    if (asset_cache_count < MAX_CACHED_ASSETS) {
        // Use next empty slot
        slot = asset_cache_count++;
    } else {
        // Find least recently used asset
        uint32_t oldest_time = 0xFFFFFFFF;
        uint32_t oldest_index = 0;
        
        for (uint32_t i = 0; i < MAX_CACHED_ASSETS; i++) {
            if (asset_cache[i].last_used < oldest_time) {
                oldest_time = asset_cache[i].last_used;
                oldest_index = i;
            }
        }
        
        // Free the LRU asset
        slot = oldest_index;
        free(asset_cache[slot].data);
    }
    
    // Store new asset
    asset_cache[slot].asset_id = asset_id;
    asset_cache[slot].data = malloc(size);
    asset_cache[slot].size = size;
    asset_cache[slot].last_used = frame_counter;
    
    // Copy data
    if (asset_cache[slot].data != NULL) {
        memcpy(asset_cache[slot].data, data, size);
    }
}

// Load asset data from file
bool load_asset_data(uint32_t asset_id, uint8_t** data, uint32_t* size) {
    // Find asset info
    AssetInfo* asset = find_asset(asset_id);
    if (asset == NULL) {
        printf("Asset not found: %lu\n", asset_id);
        return false;
    }
    
    // Check if asset is already loaded in cache
    uint8_t* cached_data = find_asset_in_cache(asset_id, size);
    if (cached_data != NULL) {
        *data = cached_data;
        return true;
    }
    
    // Make sure asset file is open
    if (!asset_file_open) {
        printf("Asset file not open\n");
        return false;
    }
    
    // Seek to asset position
    FRESULT fr = f_lseek(&asset_file, asset->offset);
    if (fr != FR_OK) {
        printf("Failed to seek to asset offset: %lu\n", asset->offset);
        return false;
    }
    
    // Allocate buffer for asset data
    uint8_t* buffer = malloc(asset->size);
    if (buffer == NULL) {
        printf("Failed to allocate memory for asset: %lu bytes\n", asset->size);
        return false;
    }
    
    // Read asset data
    UINT br;
    fr = f_read(&asset_file, buffer, asset->size, &br);
    if (fr != FR_OK || br != asset->size) {
        printf("Failed to read asset data: %lu bytes\n", asset->size);
        free(buffer);
        return false;
    }
    
    // Cache the asset
    cache_asset(asset_id, buffer, asset->size);
    
    // Return asset data
    *data = buffer;
    *size = asset->size;
    
    // Mark as loaded
    asset->loaded = true;
    
    return true;
}

// Send asset to target device (GPU or APU)
bool send_asset_to_device(uint32_t asset_id) {
    // Load asset data
    uint8_t* data = NULL;
    uint32_t size = 0;
    
    if (!load_asset_data(asset_id, &data, &size)) {
        return false;
    }
    
    // Find asset info
    AssetInfo* asset = find_asset(asset_id);
    if (asset == NULL) {
        free(data);
        return false;
    }
    
    bool result = false;
    
    // Send to appropriate device
    switch (asset->target) {
        case 1: // GPU
            result = send_asset_to_gpu(asset, data, size);
            break;
            
        case 2: // APU
            result = send_asset_to_apu(asset, data, size);
            break;
            
        default: // CPU or unknown
            result = true; // Already loaded to CPU memory
            break;
    }
    
    // Free data if we don't need to keep it
    if (asset->target != 0) {
        free(data);
    }
    
    return result;
}

// Send asset to GPU
bool send_asset_to_gpu(AssetInfo* asset, uint8_t* data, uint32_t size) {
    // Determine appropriate GPU command based on asset type
    uint8_t cmd_id;
    uint8_t header[16]; // Command header buffer
    uint8_t header_size = 0;
    
    switch (asset->type) {
        case ASSET_TYPE_TILESET:
            cmd_id = 0x21; // LOAD_TILESET
            
            // Prepare header (layer, start tile, count, compression)
            header[0] = 0; // Default to layer 0
            header[1] = 0; // Tile start index high byte
            header[2] = 0; // Tile start index low byte
            header[3] = (size / 64) >> 8; // Assuming 8x8 tiles, 8bpp
            header[4] = (size / 64) & 0xFF;
            header[5] = 0; // No compression
            header_size = 6;
            break;
            
        case ASSET_TYPE_TILEMAP:
            cmd_id = 0x22; // LOAD_TILEMAP
            
            // Prepare header (layer, x, y, width, height, compression)
            header[0] = 0; // Default to layer 0
            header[1] = 0; // X position
            header[2] = 0; // Y position
            header[3] = 32; // Default width in tiles
            header[4] = size / (32 * 2); // Height based on size (assuming 2 bytes per tile)
            header[5] = 0; // No compression
            header_size = 6;
            break;
            
        case ASSET_TYPE_SPRITE:
            cmd_id = 0x40; // LOAD_SPRITE_PATTERN
            
            // Prepare header (pattern ID, width, height, bpp, compression)
            header[0] = asset->id & 0xFF; // Pattern ID
            header[1] = 2; // Width in 8-pixel units (16 pixels)
            header[2] = 2; // Height in 8-pixel units (16 pixels)
            header[3] = 8; // 8bpp
            header[4] = 0; // No compression
            header_size = 5;
            break;
            
        case ASSET_TYPE_PALETTE:
            cmd_id = 0x11; // LOAD_PALETTE
            
            // Prepare header (start index, count)
            header[0] = 0; // Start index
            header[1] = size / 3; // Count (3 bytes per color: RGB)
            header_size = 2;
            break;
            
        default:
            printf("Unsupported asset type for GPU: %d\n", asset->type);
            return false;
    }
    
    // Send command header
    uint8_t cmd_buffer[header_size + size];
    memcpy(cmd_buffer, header, header_size);
    memcpy(cmd_buffer + header_size, data, size);
    
    // Queue command
    return queue_gpu_command(cmd_id, header_size + 2, cmd_buffer);
}

// Send asset to APU
bool send_asset_to_apu(AssetInfo* asset, uint8_t* data, uint32_t size) {
    // Determine appropriate APU command based on asset type
    uint8_t cmd_id;
    uint8_t header[16]; // Command header buffer
    uint8_t header_size = 0;
    
    switch (asset->type) {
        case ASSET_TYPE_SAMPLE:
            cmd_id = 0x70; // SAMPLE_LOAD
            
            // Prepare header (sample ID, format, sample rate, loop points, size)
            header[0] = asset->id & 0xFF; // Sample ID
            header[1] = 0; // Format (8-bit mono)
            header[2] = 44; // Sample rate (11025Hz) low byte
            header[3] = 43; // Sample rate high byte
            header[4] = 0; // Loop start low byte
            header[5] = 0; // Loop start high byte
            header[6] = 0; // Loop end low byte
            header[7] = 0; // Loop end high byte
            header[8] = size & 0xFF; // Size low byte
            header[9] = (size >> 8) & 0xFF; // Size high byte
            header_size = 10;
            break;
            
        case ASSET_TYPE_MUSIC:
            cmd_id = 0x10; // TRACKER_LOAD
            
            // Prepare header (tracker ID, data size)
            header[0] = asset->id & 0xFF; // Tracker ID
            header[1] = size & 0xFF; // Size low byte
            header[2] = (size >> 8) & 0xFF; // Size high byte
            header_size = 3;
            break;
            
        default:
            printf("Unsupported asset type for APU: %d\n", asset->type);
            return false;
    }
    
    // For large assets like samples, we need to send in chunks
    if (asset->type == ASSET_TYPE_SAMPLE && size > 240) {
        // First send the header
        if (!queue_apu_command(cmd_id, header_size + 2, header)) {
            return false;
        }
        
        // Then send the data in chunks
        const uint16_t CHUNK_SIZE = 240;
        uint32_t remaining = size;
        uint32_t offset = 0;
        
        while (remaining > 0) {
            uint16_t chunk_size = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
            
            // Use custom data transfer command
            uint8_t* chunk_data = data + offset;
            
            if (!queue_apu_command(0xF0, chunk_size + 2, chunk_data)) {
                return false;
            }
            
            offset += chunk_size;
            remaining -= chunk_size;
        }
        
        return true;
    } else {
        // Send command with header and data
        uint8_t cmd_buffer[header_size + size];
        memcpy(cmd_buffer, header, header_size);
        memcpy(cmd_buffer + header_size, data, size);
        
        // Queue command
        return queue_apu_command(cmd_id, header_size + size + 2, cmd_buffer);
    }
}

// QSPI Asset Loading Implementation
bool load_game_qspi(GameInfo game) {
    // Open game file on QSPI storage
    FILE* game_file = open_qspi_game_file(game.filename);
    if (!game_file) return false;

    // Read game header
    GameHeader header;
    read_game_header(game_file, &header);

    // Allocate memory for game code
    void* game_code_memory = allocate_game_memory(header.code_size);
    load_game_code(game_file, game_code_memory, header.code_size);

    // Load assets into GPU & APU memory
    load_gpu_assets_qspi(game_file, &header.gpu_assets);
    load_apu_assets_qspi(game_file, &header.apu_assets);

    close_file(game_file);
    return true;
}

// Input Processing
// Button states
typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool a;
    bool b;
    bool start;
    bool select;
} ButtonState;

// Current and previous button states
ButtonState current_buttons;
ButtonState previous_buttons;

// Button debounce counters
uint8_t debounce_counters[8] = {0};
#define DEBOUNCE_THRESHOLD 3

// Initialize input system
void init_input() {
    // Clear button states
    memset(&current_buttons, 0, sizeof(ButtonState));
    memset(&previous_buttons, 0, sizeof(ButtonState));
    
    // Reset debounce counters
    memset(debounce_counters, 0, sizeof(debounce_counters));
}

// Read raw button states
ButtonState read_raw_buttons() {
    ButtonState raw;

    // Buttons are active low (pulled up when not pressed)
    raw.up = !gpio_get(BTN_UP_PIN);
    raw.down = !gpio_get(BTN_DOWN_PIN);
    raw.left = !gpio_get(BTN_LEFT_PIN);
    raw.right = !gpio_get(BTN_RIGHT_PIN);
    raw.a = !gpio_get(BTN_A_PIN);
    raw.b = !gpio_get(BTN_B_PIN);
    raw.start = !gpio_get(BTN_START_PIN);
    raw.select = !gpio_get(BTN_SELECT_PIN);
    
    return raw;
}

// Update button states with debouncing
void update_buttons() {
    // Save previous state
    previous_buttons = current_buttons;
    
    // Read raw button states
    ButtonState raw = read_raw_buttons();
    
    // Apply debouncing to each button
    update_button_with_debounce(&current_buttons.up, raw.up, &debounce_counters[0]);
    update_button_with_debounce(&current_buttons.down, raw.down, &debounce_counters[1]);
    update_button_with_debounce(&current_buttons.left, raw.left, &debounce_counters[2]);
    update_button_with_debounce(&current_buttons.right, raw.right, &debounce_counters[3]);
    update_button_with_debounce(&current_buttons.a, raw.a, &debounce_counters[4]);
    update_button_with_debounce(&current_buttons.b, raw.b, &debounce_counters[5]);
    update_button_with_debounce(&current_buttons.start, raw.start, &debounce_counters[6]);
    update_button_with_debounce(&current_buttons.select, raw.select, &debounce_counters[7]);
}

// Apply debouncing to a single button
void update_button_with_debounce(bool* button_state, bool raw_state, uint8_t* counter) {
    if (raw_state != *button_state) {
        // Button state is different from current state
        (*counter)++;
        
        if (*counter >= DEBOUNCE_THRESHOLD) {
            // Debounce threshold reached, update state
            *button_state = raw_state;
            *counter = 0;
        }
    } else {
        // Button state matches current state, reset counter
        *counter = 0;
    }
}

// Check if a button was just pressed
bool button_pressed(bool current, bool previous) {
    return current && !previous;
}

// Check if a button was just released
bool button_released(bool current, bool previous) {
    return !current && previous;
}

// Get directional input as a vector
void get_direction(int* dx, int* dy) {
    *dx = 0;
    *dy = 0;
    
    if (current_buttons.left) *dx -= 1;
    if (current_buttons.right) *dx += 1;
    if (current_buttons.up) *dy -= 1;
    if (current_buttons.down) *dy += 1;
}

// Inter-Core Communication
// Message types for inter-core communication
typedef enum {
    MSG_LOAD_ASSET,
    MSG_PROCESS_GPU_QUEUE,
    MSG_PROCESS_APU_QUEUE,
    MSG_GAME_EVENT
} MessageType;

// Message structure
typedef struct {
    MessageType type;
    uint32_t param1;
    uint32_t param2;
    void* data;
} CoreMessage;

// Inter-core queues
queue_t core0_to_core1_queue;
queue_t core1_to_core0_queue;

// Initialize inter-core communication
void init_inter_core_communication() {
    queue_init(&core0_to_core1_queue, sizeof(CoreMessage), 8);
    queue_init(&core1_to_core0_queue, sizeof(CoreMessage), 8);
}

// Send a message from Core 0 to Core 1
void send_message_to_core1(MessageType type, uint32_t param1, uint32_t param2, void* data) {
    CoreMessage msg = {
        .type = type,
        .param1 = param1,
        .param2 = param2,
        .data = data
    };
    
    queue_add_blocking(&core0_to_core1_queue, &msg);
}

// Send a message from Core 1 to Core 0
void send_message_to_core0(MessageType type, uint32_t param1, uint32_t param2, void* data) {
    CoreMessage msg = {
        .type = type,
        .param1 = param1,
        .param2 = param2,
        .data = data
    };
    
    queue_add_blocking(&core1_to_core0_queue, &msg);
}

// Core 1 main function - handles system management
void core1_main() {
    printf("CPU Core 1 started - System Management\n");
    
    CoreMessage msg;
    bool vsync_pending = false;
    
    while (true) {
        // Check for messages from Core 0
        if (queue_try_remove(&core0_to_core1_queue, &msg)) {
            // Process message
            switch (msg.type) {
                case MSG_LOAD_ASSET:
                    {
                        uint32_t asset_id = msg.param1;
                        send_asset_to_device(asset_id);
                    }
                    break;
                    
                case MSG_PROCESS_GPU_QUEUE:
                    process_gpu_queue();
                    break;
                    
                case MSG_PROCESS_APU_QUEUE:
                    process_apu_queue();
                    break;
                    
                case MSG_GAME_EVENT:
                    // Game-specific events
                    handle_game_event(msg.param1, msg.param2, msg.data);
                    break;
            }
        }
        
        // Process GPU and APU queues regularly
        if (gpu_queue.count > 0) {
            process_gpu_queue();
        }
        
        if (apu_queue.count > 0) {
            process_apu_queue();
        }
        
        // Check for VSYNC signal from GPU
        if (!gpio_get(VSYNC_PIN) && !vsync_pending) {
            vsync_pending = true;
            
            // Notify Core 0
            send_message_to_core0(MSG_GAME_EVENT, EVENT_VSYNC, 0, NULL);
        } else if (gpio_get(VSYNC_PIN)) {
            vsync_pending = false;
        }
        
        // Yield to save power
        sleep_us(100);
    }
}

// Game Management and State
// Game state structure
typedef struct {
    uint32_t game_id;
    char title[32];
    uint32_t score;
    uint32_t level;
    uint32_t lives;
    bool game_active;
    bool paused;
    
    // Game-specific data
    uint8_t game_data[1024];
} GameState;

// Current game state
GameState game_state;

// Initialize default game state
void init_game_state() {
    memset(&game_state, 0, sizeof(GameState));
    game_state.lives = 3;
    game_state.level = 1;
    game_state.game_active = false;
    game_state.paused = false;
}

// Load a game from SD card
bool load_game(const char* filename) {
    FIL f;
    FRESULT fr;
    
    // Open game file
    fr = f_open(&f, filename, FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open game file: %s\n", filename);
        return false;
    }
    
    // Read game header
    GameHeader header;
    UINT br;
    fr = f_read(&f, &header, sizeof(GameHeader), &br);
    if (fr != FR_OK || br != sizeof(GameHeader)) {
        printf("Failed to read game header\n");
        f_close(&f);
        return false;
    }
    
    // Verify header magic
    if (memcmp(header.magic, "TBOY", 4) != 0) {
        printf("Invalid game file format\n");
        f_close(&f);
        return false;
    }
    
    // Initialize game state
    init_game_state();
    game_state.game_id = header.game_id;
    strncpy(game_state.title, header.title, sizeof(game_state.title));
    
    // Load asset registry
    load_asset_registry(header.asset_registry);
    
    // Open asset file
    open_asset_file(header.asset_file);
    
    // Load game code
    if (header.code_size > 0) {
        // Placeholder for loading executable code
        // In a real implementation, this would load actual executable code
        printf("Loading game code: %lu bytes\n", header.code_size);
    }
    
    // Close file
    f_close(&f);
    
    // Load essential assets (first N assets marked as required)
    for (uint32_t i = 0; i < asset_count && i < 20; i++) {
        if (assets[i].id < 100) { // Assuming IDs < 100 are essential
            send_asset_to_device(assets[i].id);
        }
    }
    
    // Initialize GPU
    init_gpu();
    
    // Initialize APU
    init_apu();
    
    game_state.game_active = true;
    
    return true;
}

// Initialize GPU for a game
void init_gpu() {
    // Reset GPU
    uint8_t reset_cmd[1] = {0};
    queue_gpu_command(0x01, 2, reset_cmd); // RESET_GPU
    
    // Set display mode (320x240x8bpp)
    uint8_t display_cmd[5] = {
        (320 >> 8) & 0xFF, 320 & 0xFF,  // Width
        (240 >> 8) & 0xFF, 240 & 0xFF,  // Height
        8                               // BPP
    };
    queue_gpu_command(0x02, 7, display_cmd); // SET_DISPLAY_MODE
    
    // Enable VSYNC notification
    uint8_t vsync_cmd[1] = {1};
    queue_gpu_command(0x03, 3, vsync_cmd); // SET_VBLANK_CALLBACK
    
    // Process commands
    process_gpu_queue();
}

// Initialize APU for a game
void init_apu() {
    // Reset APU
    uint8_t reset_cmd[1] = {0};
    queue_apu_command(0x01, 2, reset_cmd); // RESET_AUDIO
    
    // Set master volume
    uint8_t volume_cmd[1] = {200}; // 0-255
    queue_apu_command(0x02, 3, volume_cmd); // SET_MASTER_VOLUME
    
    // Process commands
    process_apu_queue();
}

// Update game state
void update_game() {
    // Game-specific update logic would go here
    // For demonstration, we'll implement a simple update
    
    if (!game_state.game_active || game_state.paused) {
        return;
    }
    
    // Update input
    update_buttons();
    
    // Get direction
    int dx = 0, dy = 0;
    get_direction(&dx, &dy);
    
    // Example: Move a sprite based on input
    static int16_t player_x = 160;
    static int16_t player_y = 120;
    
    player_x += dx * 2;
    player_y += dy * 2;
    
    // Keep player on screen
    if (player_x < 8) player_x = 8;
    if (player_x > 312) player_x = 312;
    if (player_y < 8) player_y = 8;
    if (player_y > 232) player_y = 232;
    
    // Update sprite position
    uint8_t move_cmd[5] = {
        0,                           // Sprite ID
        (player_x >> 8) & 0xFF, player_x & 0xFF,  // X position
        (player_y >> 8) & 0xFF, player_y & 0xFF   // Y position
    };
    queue_gpu_command(0x42, 6, move_cmd); // MOVE_SPRITE
    
    // Check for A button press
    if (button_pressed(current_buttons.a, previous_buttons.a)) {
        // Play sound effect
        uint8_t sound_cmd[4] = {
            0,    // Channel
            1,    // Sample ID
            128,  // Pitch (normal)
            200   // Volume
        };
        queue_apu_command(0x71, 5, sound_cmd); // SAMPLE_PLAY
    }
    
    // Check for start button to pause
    if (button_pressed(current_buttons.start, previous_buttons.start)) {
        game_state.paused = !game_state.paused;
    }
    
    // Update frame counter
    frame_counter++;
}

// Main game loop with enhanced synchronization
void run_enhanced_game_loop() {
    // Game loop variables
    uint32_t frame_start_time;
    uint32_t frame_time;
    const uint32_t TARGET_FRAME_TIME = 16667; // 60fps in microseconds

    while (true) {
        // Start frame timing
        frame_start_time = time_us_32();

        // Check for responses from GPU/APU
        check_for_device_responses();

        // Update frame counter and sync
        update_frame_timing();

        // Check device health periodically (every 10 frames)
        if (global_frame_counter % 10 == 0) {
            if (!check_device_health(1)) {
                // GPU health check failed
                handle_device_failure(1);
            }

            if (!check_device_health(2)) {
                // APU health check failed
                handle_device_failure(2);
            }
        }

        // Update game state only if devices are healthy
        if (!in_system_recovery) {
            process_input();
            update_game_state();
            prepare_rendering();
        } else {
            // In recovery mode - display error screen
            display_system_error();
        }

        // Process command queues regardless of game state
        // (allows recovery commands to be sent)
        process_enhanced_queue(&gpu_queue);
        process_enhanced_queue(&apu_queue);

        // Calculate frame time
        frame_time = time_us_32() - frame_start_time;

        // Sleep to maintain target frame rate
        if (frame_time < TARGET_FRAME_TIME) {
            sleep_us(TARGET_FRAME_TIME - frame_time);
        } else if (frame_time > TARGET_FRAME_TIME + 5000) {
            // Frame took too long (>5ms over budget)
            if (debug_enabled) {
                printf("Frame time: %lu us (over budget)\n", frame_time);
            }
        }
    }
}

void handle_device_failure(uint8_t device_id) {
    // Mark system as in recovery
    in_system_recovery = true;

    // Log the failure
    if (debug_enabled) {
        printf("CRITICAL: Device %d failure detected\n", device_id);
    }

    // Attempt recovery steps
    if (device_id == 1) {
        // GPU recovery
        gpu_recovery_attempts++;

        if (gpu_recovery_attempts <= MAX_RECOVERY_ATTEMPTS) {
            reset_communication_interface(1);
            send_gpu_reset_command();
        } else {
            // Too many failed attempts
            system_error = ERR_GPU_FAILURE;
        }
    } else {
        // APU recovery
        apu_recovery_attempts++;

        if (apu_recovery_attempts <= MAX_RECOVERY_ATTEMPTS) {
            reset_communication_interface(2);
            send_apu_reset_command();
        } else {
            // Too many failed attempts
            system_error = ERR_APU_FAILURE;
        }
    }
}

// Main Game Loop
void run_game_loop() {
    // Main game loop runs on Core 0
    
    // Initialize game timer
    uint32_t frame_start_time = time_us_32();
    uint32_t target_frame_time = 16667; // ~60fps (microseconds)
    
    // Wait for VSYNC
    bool vsync_received = false;
    
    while (game_state.game_active) {
        // Start frame timing
        frame_start_time = time_us_32();
        
        // Process any messages from Core 1
        CoreMessage msg;
        while (queue_try_remove(&core1_to_core0_queue, &msg)) {
            if (msg.type == MSG_GAME_EVENT && msg.param1 == EVENT_VSYNC) {
                vsync_received = true;
            }
        }
        
        // Process game logic
        update_game();
        
        // Prepare rendering for next frame
        prepare_rendering();
        
        // Send message to Core 1 to process GPU commands
        send_message_to_core1(MSG_PROCESS_GPU_QUEUE, 0, 0, NULL);
        
        // Prepare audio commands
        prepare_audio();
        
        // Send message to Core 1 to process APU commands
        send_message_to_core1(MSG_PROCESS_APU_QUEUE, 0, 0, NULL);
        
        // Check if we need to load any new assets
        check_asset_requirements();
        
        // Calculate frame time
        uint32_t frame_end_time = time_us_32();
        uint32_t frame_duration = frame_end_time - frame_start_time;
        
        // Wait for VSYNC if needed
        if (!vsync_received) {
            // Wait until we get VSYNC or timeout
            uint32_t wait_start = time_us_32();
            while (!vsync_received && time_us_32() - wait_start < 20000) {
                // Check for VSYNC message
                if (queue_try_remove(&core1_to_core0_queue, &msg)) {
                    if (msg.type == MSG_GAME_EVENT && msg.param1 == EVENT_VSYNC) {
                        vsync_received = true;
                    }
                }
                sleep_us(100);
            }
        }
        
        // Reset VSYNC flag
        vsync_received = false;
        
        // Sleep if we have time left to maintain target frame rate
        if (frame_duration < target_frame_time) {
            sleep_us(target_frame_time - frame_duration);
        }
    }
}

// Prepare rendering commands for GPU
void prepare_rendering() {
    // This would typically generate all the GPU commands needed for the frame
    // For demonstration, we'll implement a simple example
    
    // Example: Scroll background layers
    static uint16_t scroll_x = 0;
    
    // Scroll the background layer
    uint8_t scroll_cmd[5] = {
        0,                        // Layer ID
        (scroll_x >> 8) & 0xFF, scroll_x & 0xFF,  // X scroll
        0, 0                      // Y scroll
    };
    queue_gpu_command(0x23, 6, scroll_cmd); // SCROLL_LAYER
    
    // Update scroll position for next frame
    scroll_x = (scroll_x + 1) % 1024;
}

// Prepare audio commands for APU
void prepare_audio() {
    // This would typically generate all the APU commands needed for the frame
    // For demonstration, we'll keep it simple
    
    // Example: Update a channel volume based on distance
    static uint8_t volume = 128;
    static int8_t vol_dir = 1;
    
    // Slowly oscillate volume
    volume += vol_dir;
    if (volume >= 240 || volume <= 128) {
        vol_dir = -vol_dir;
    }
    
    // Set channel volume
    uint8_t vol_cmd[2] = {
        0,      // Channel ID
        volume  // Volume
    };
    queue_apu_command(0x30, 3, vol_cmd); // CHANNEL_SET_VOLUME
}

// Check if any new assets need to be loaded
void check_asset_requirements() {
    // This function would check if any new assets are needed based on game state, player position, etc.    
    // For demonstration, we'll implement a simple version that loads assets based on level
    
    static uint32_t last_level = 0;
    
    if (game_state.level != last_level) {
        // Level changed, load new assets
        uint32_t level_start_id = 100 + (game_state.level - 1) * 10;
        
        for (uint32_t i = 0; i < 10; i++) {
            uint32_t asset_id = level_start_id + i;
            
            // Find the asset
            AssetInfo* asset = find_asset(asset_id);
            if (asset != NULL && !asset->loaded) {
                // Queue the asset for loading
                send_message_to_core1(MSG_LOAD_ASSET, asset_id, 0, NULL);
            }
        }
        
        last_level = game_state.level;
    }
}

// Boot Process and Main Function
// Boot sequence
void boot_sequence() {
    // Initialize hardware
    init_hardware();
    
    // Initialize subsystems
    init_command_queues();
    init_asset_system();
    init_input();
    init_inter_core_communication();
    
    // Mount SD card
    FRESULT fr = f_mount(&fatfs, "", 1);
    if (fr != FR_OK) {
        printf("Failed to mount SD card: %d\n", fr);
        // Display error on screen
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
    load_game(selected_game.filename);
}

// Load system configuration
void load_system_config() {
    FIL f;
    FRESULT fr;
    
    // Try to open config file
    fr = f_open(&f, "config.ini", FA_READ);
    if (fr == FR_OK) {
        // Read configuration (simplified)
        char line[64];
        while (f_gets(line, sizeof(line), &f)) {
            // Parse configuration lines
            parse_config_line(line);
        }
        
        f_close(&f);
    } else {
        // Create default configuration
        create_default_config();
    }
}

// Initialize GPU for boot
void initialize_gpu() {
    // Reset GPU
    uint8_t reset_cmd[1] = {0};
    queue_gpu_command(0x01, 2, reset_cmd); // RESET_GPU
    
    // Set display mode (320x240x8bpp)
    uint8_t display_cmd[5] = {
        (320 >> 8) & 0xFF, 320 & 0xFF,  // Width
        (240 >> 8) & 0xFF, 240 & 0xFF,  // Height
        8                               // BPP
    };
    queue_gpu_command(0x02, 7, display_cmd); // SET_DISPLAY_MODE
    
    // Process commands
    process_gpu_queue();
}

// Initialize APU for boot
void initialize_apu() {
    // Reset APU
    uint8_t reset_cmd[1] = {0};
    queue_apu_command(0x01, 2, reset_cmd); // RESET_AUDIO
    
    // Set master volume
    uint8_t volume_cmd[1] = {200}; // 0-255
    queue_apu_command(0x02, 3, volume_cmd); // SET_MASTER_VOLUME
    
    // Process commands
    process_apu_queue();
}

// Load and display boot screen
void load_boot_assets() {
    // Load boot logo
    FIL f;
    FRESULT fr;
    
    fr = f_open(&f, "boot/logo.bin", FA_READ);
    if (fr == FR_OK) {
        // Read logo data
        uint32_t size = f_size(&f);
        uint8_t* logo_data = malloc(size);
        
        UINT br;
        fr = f_read(&f, logo_data, size, &br);
        
        if (fr == FR_OK && br == size) {
            // Send logo to GPU as a sprite
            uint8_t header[5] = {
                0,    // Pattern ID
                10,   // Width in 8-pixel units
                5,    // Height in 8-pixel units
                8,    // 8bpp
                0     // No compression
            };
            
            uint8_t* cmd_data = malloc(size + 5);
            memcpy(cmd_data, header, 5);
            memcpy(cmd_data + 5, logo_data, size);
            
            queue_gpu_command(0x40, 5 + 2, cmd_data); // LOAD_SPRITE_PATTERN
            
            free(cmd_data);
        }
        
        free(logo_data);
        f_close(&f);
    }
    
    // Load boot music
    fr = f_open(&f, "boot/music.bin", FA_READ);
    if (fr == FR_OK) {
        // Read music data
        uint32_t size = f_size(&f);
        uint8_t* music_data = malloc(size);
        
        UINT br;
        fr = f_read(&f, music_data, size, &br);
        
        if (fr == FR_OK && br == size) {
            // Send music to APU
            uint8_t header[3] = {
                0,    // Tracker ID
                size & 0xFF,
                (size >> 8) & 0xFF
            };
            
            uint8_t* cmd_data = malloc(size + 3);
            memcpy(cmd_data, header, 3);
            memcpy(cmd_data + 3, music_data, size);
            
            queue_apu_command(0x10, 3 + 2, cmd_data); // TRACKER_LOAD
            
            free(cmd_data);
        }
        
        free(music_data);
        f_close(&f);
    }
    
    // Process commands
    process_gpu_queue();
    process_apu_queue();
}

// Display boot screen
void display_boot_screen() {
    // Place the logo sprite
    uint8_t sprite_cmd[9] = {
        0,    // Sprite ID
        0,    // Pattern ID
        (160 >> 8) & 0xFF, 160 & 0xFF,  // X position (center)
        (100 >> 8) & 0xFF, 100 & 0xFF,  // Y position
        0,    // Attributes
        0,    // Palette offset
        128   // Scale (1.0)
    };
    queue_gpu_command(0x41, 10, sprite_cmd); // DEFINE_SPRITE
    
    // Draw "Press START" text
    // This would use direct drawing commands in a real implementation
    
    // Play boot music
    uint8_t play_cmd[1] = {0}; // Tracker ID
    queue_apu_command(0x11, 2, play_cmd); // TRACKER_PLAY
    
    // Process commands
    process_gpu_queue();
    process_apu_queue();
    
    // Wait for START button
    bool start_pressed = false;
    
    while (!start_pressed) {
        // Update input
        update_buttons();
        
        // Check for START button
        if (button_pressed(current_buttons.start, previous_buttons.start)) {
            start_pressed = true;
        }
        
        // Small delay
        sleep_ms(10);
    }
    
    // Stop music
    uint8_t stop_cmd[1] = {0}; // Tracker ID
    queue_apu_command(0x12, 2, stop_cmd); // TRACKER_STOP
    
    process_apu_queue();
}

// Main function
int main() {
    // Run boot sequence
    boot_sequence();
    
    // Launch Core 1
    multicore_launch_core1(core1_main);
    
    // Wait for Core 1 to start
    sleep_ms(100);
    
    // Enter game loop on Core 0
    run_game_loop();
    
    // If we exit the game loop, return to the game selection menu
    // In a real implementation, this would show a game over screen
    // and then return to the menu
    
    return 0;
}

// High-Level Game Development APIs
// Sprite management
void set_sprite(uint8_t sprite_id, uint8_t pattern_id, int16_t x, int16_t y, uint8_t attributes) {
    uint8_t data[9] = {
        sprite_id,
        pattern_id,
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF,
        attributes,
        0,    // Default palette offset
        128   // Default scale (1.0)
    };
    
    queue_gpu_command(0x41, 10, data); // DEFINE_SPRITE
}

// Move sprite
void move_sprite(uint8_t sprite_id, int16_t x, int16_t y) {
    uint8_t data[5] = {
        sprite_id,
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF
    };
    
    queue_gpu_command(0x42, 6, data); // MOVE_SPRITE
}

// Animate sprite
void animate_sprite(uint8_t sprite_id, uint8_t start_frame, uint8_t end_frame, uint8_t frame_rate) {
    uint8_t data[5] = {
        sprite_id,
        start_frame,
        end_frame,
        frame_rate,
        1  // Loop mode (0=once, 1=loop, 2=ping-pong)
    };
    
    queue_gpu_command(0x46, 6, data); // ANIMATE_SPRITE
}

// Background scrolling
void scroll_background(uint8_t layer_id, int16_t x, int16_t y) {
    uint8_t data[5] = {
        layer_id,
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF
    };
    
    queue_gpu_command(0x23, 6, data); // SCROLL_LAYER
}

// Audio control
void play_sound_effect(uint8_t channel, uint8_t sample_id, uint8_t pitch, uint8_t volume) {
    uint8_t data[4] = {
        channel,
        sample_id,
        pitch,
        volume
    };
    
    queue_apu_command(0x71, 5, data); // SAMPLE_PLAY
}

void play_music(uint8_t tracker_id) {
    uint8_t data[1] = {tracker_id};
    queue_apu_command(0x11, 2, data); // TRACKER_PLAY
}

void stop_music(uint8_t tracker_id) {
    uint8_t data[1] = {tracker_id};
    queue_apu_command(0x12, 2, data); // TRACKER_STOP
}

// Asset management helpers
bool preload_asset(uint32_t asset_id) {
    return send_asset_to_device(asset_id);
}

// Drawing functions
void draw_pixel(int16_t x, int16_t y, uint8_t color) {
    uint8_t data[5] = {
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF,
        color
    };
    
    queue_gpu_command(0x80, 6, data); // DRAW_PIXEL
}

void draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color) {
    uint8_t data[9] = {
        (x1 >> 8) & 0xFF, x1 & 0xFF,
        (y1 >> 8) & 0xFF, y1 & 0xFF,
        (x2 >> 8) & 0xFF, x2 & 0xFF,
        (y2 >> 8) & 0xFF, y2 & 0xFF,
        color
    };
    
    queue_gpu_command(0x81, 10, data); // DRAW_LINE
}

void draw_rect(int16_t x, int16_t y, uint16_t width, uint16_t height, uint8_t color, bool fill) {
    uint8_t data[9] = {
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF,
        (width >> 8) & 0xFF, width & 0xFF,
        (height >> 8) & 0xFF, height & 0xFF,
        color,
        fill ? 1 : 0
    };
    
    queue_gpu_command(0x82, 11, data); // DRAW_RECT
}

// RP2350-Specific Enhancements
// Check if running on RP2350
bool check_if_rp2350() {
    // This is implementation-specific and would depend on
    // how to detect the chip type
    
    // A simple approach might be to check available RAM
    uint32_t* ram_test = malloc(400 * 1024); // Try to allocate 400KB
    bool is_rp2350 = (ram_test != NULL);
    
    // Free the test allocation
    if (ram_test != NULL) {
        free(ram_test);
    }
    
    return is_rp2350;
}

// Enhanced asset management for RP2350
void init_enhanced_asset_management() {
    if (!check_if_rp2350()) return;
    
    // Increase asset cache size
    #define ENHANCED_MAX_CACHED_ASSETS 64
    
    // Reallocate asset cache with larger size
    AssetCacheEntry* new_cache = malloc(sizeof(AssetCacheEntry) * ENHANCED_MAX_CACHED_ASSETS);
    
    if (new_cache != NULL) {
        // Copy existing cache entries
        memcpy(new_cache, asset_cache, sizeof(AssetCacheEntry) * asset_cache_count);
        
        // Initialize new entries
        for (int i = asset_cache_count; i < ENHANCED_MAX_CACHED_ASSETS; i++) {
            new_cache[i].asset_id = 0;
            new_cache[i].data = NULL;
            new_cache[i].size = 0;
            new_cache[i].last_used = 0;
        }
        
        // Replace old cache
        free(asset_cache);
        asset_cache = new_cache;
        
        printf("Enhanced asset cache initialized: %d entries\n", ENHANCED_MAX_CACHED_ASSETS);
    }
}

// Advanced parallax background for RP2350
void setup_advanced_parallax(uint8_t num_layers) {
    if (!check_if_rp2350()) return;
    
    // Configure multiple parallax layers
    for (uint8_t i = 0; i < num_layers && i < 4; i++) {
        uint8_t layer_config[8] = {
            i,                  // Layer ID
            1,                  // Enable
            i,                  // Priority
            i == 0 ? 2 : 1,     // Scroll mode (line scroll for first layer)
            8,                  // Tile width
            8,                  // Tile height
            64,                 // Width in tiles
            32                  // Height in tiles
        };
        
        queue_gpu_command(0x20, 9, layer_config); // CONFIGURE_LAYER
        
        // For layer 0, set up line scroll table
        if (i == 0) {
            // Allocate buffer for H-scroll values
            uint8_t* hscroll_data = malloc(240 * 2 + 2);
            hscroll_data[0] = 0; // Layer ID
            hscroll_data[1] = 0; // Start line
            hscroll_data[2] = 240; // Line count
            
            // Generate wavy parallax effect
            for (int j = 0; j < 240; j++) {
                uint16_t scroll_val = (uint16_t)(sinf(j * 0.05f) * 20.0f);
                hscroll_data[3 + j*2] = (scroll_val >> 8) & 0xFF;
                hscroll_data[4 + j*2] = scroll_val & 0xFF;
            }
            
            queue_gpu_command(0x24, 240 * 2 + 3, hscroll_data); // SET_HSCROLL_TABLE
            free(hscroll_data);
        }
    }
}

// Enhanced tilemap loading with RLE compression (RP2350)
bool load_compressed_tilemap(uint8_t layer_id, const char* filename) {
    if (!check_if_rp2350()) return false;
    
    FIL f;
    FRESULT fr;
    
    // Open tilemap file
    fr = f_open(&f, filename, FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open tilemap: %s\n", filename);
        return false;
    }
    
    // Read header
    uint8_t width, height;
    UINT br;
    fr = f_read(&f, &width, 1, &br);
    fr = f_read(&f, &height, 1, &br);
    
    if (fr != FR_OK || br != 1) {
        f_close(&f);
        return false;
    }
    
    // Read compressed data
    uint32_t compressed_size = f_size(&f) - 2; // Subtract header
    uint8_t* compressed_data = malloc(compressed_size);
    
    fr = f_read(&f, compressed_data, compressed_size, &br);
    if (fr != FR_OK || br != compressed_size) {
        free(compressed_data);
        f_close(&f);
        return false;
    }
    
    f_close(&f);
    
    // Prepare command data
    uint8_t header[6] = {
        layer_id,
        0, 0,         // X, Y position
        width,
        height,
        1            // RLE compression
    };
    
    // Combine header and data
    uint8_t* command_data = malloc(compressed_size + 6);
    memcpy(command_data, header, 6);
    memcpy(command_data + 6, compressed_data, compressed_size);
    
    // Send command to GPU
    queue_gpu_command(0x22, compressed_size + 8, command_data);
    
    // Free memory
    free(compressed_data);
    free(command_data);
    
    return true;
}

// Multi-channel audio streaming (RP2350)
bool start_multi_channel_audio(uint8_t* channel_ids, uint8_t* sample_ids, uint8_t channel_count) {
    if (!check_if_rp2350()) return false;
    
    // Set up multiple audio channels at once
    for (uint8_t i = 0; i < channel_count; i++) {
        uint8_t data[4] = {
            channel_ids[i],   // Channel ID
            sample_ids[i],    // Sample ID
            128,              // Pitch (normal)
            200               // Volume
        };
        
        queue_apu_command(0x71, 5, data); // SAMPLE_PLAY
    }
    
    // Process commands
    process_apu_queue();
    
    return true;
}

/*
This comprehensive CPU implementation provides all the key functionality for the TriBoy system as described in the documentation, including:

1. **Core System Management**:
   - Dual-core operation (game logic on Core 0, system management on Core 1)
   - Command queue management for GPU and APU
   - Asset loading and resource management
   - Input processing with debouncing
   - SD card file system handling

2. **Game Management**:
   - Game loading and initialization
   - Main game loop with proper timing
   - Game state management
   - Frame synchronization with VSYNC

3. **Asset System**:
   - Asset registry and loading from SD card
   - Asset caching for frequently-used assets
   - Efficient management of limited memory
   - Distribution of assets to appropriate devices (GPU/APU)

4. **High-Level APIs**:
   - Simple sprite management
   - Background layer control
   - Audio playback
   - Drawing utilities
   - Input handling

5. **Boot Process**:
   - System initialization
   - SD card mounting
   - Display of boot screen
   - Game selection menu
*/
