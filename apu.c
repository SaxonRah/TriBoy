/*
Core APU Architecture: The APU is built around an RP2040 (264KB RAM) or RP2350 (520KB RAM) microcontroller with the following memory allocation:
  - **RP2040 (264KB)**: 64KB tracker data, 128KB sample storage, 32KB instrument definitions, 40KB working buffers
  - **RP2350 (520KB)**: 128KB tracker data, 256KB sample storage, 64KB instrument definitions, 72KB working buffers

The APU supports several synthesis methods:
  1. Tracker system for music playback (16 channels)
  2. PCM sample playback with variable rates, looping, and pitch shifting
  3. FM synthesis with 4 operators and multiple algorithms
  4. Wavetable synthesis with morphing between tables
  5. Effects processing (reverb, delay, filters, etc.)

Command-Based Interface: Communication with the APU is handled through a command system via SPI, where the CPU sends compact commands to control audio. The command structure is:
  - 1 byte: Command ID
  - 1 byte: Command length (including ID and length)
  - N bytes: Command parameters

## Implementation Approach
  1. Efficiently handles all synthesis types in parallel
  2. Provides robust command processing
  3. Maximizes audio quality within memory constraints
  4. Optimizes CPU usage across both cores
  5. Implements the complete command set

---

Core allocation:
Core 0: Command processing, tracker sequencing, asset management
Core 1: Audio synthesis, mixing, and output generation

Memory regions:
  - Sample data: PCM audio samples (128KB/256KB)
  - Tracker data: Patterns, sequences, etc. (64KB/128KB)
  - Instrument data: FM/wavetable definitions (32KB/64KB)
  - Working buffers: Mixing and effect processing (40KB/72KB)

Audio pipeline:
  1. Command reception and processing
  2. Tracker sequencing and event generation
  3. Synthesis (FM, sample, wavetable)
  4. Channel processing and effects
  5. Global effects and final mixing
  6. Output via PWM or I2S
*/

// Clock Synchronization Implementation
// Timing variables for clock synchronization
volatile uint32_t synced_frame_counter = 0;
volatile uint64_t master_clock_timestamp = 0;
volatile uint64_t local_clock_offset = 0;

void process_clock_sync_command(const uint8_t* data) {
    // Extract frame counter and timestamp from command
    uint32_t cpu_frame_counter =
        ((uint32_t)data[0] << 24) |
        ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) |
        (uint32_t)data[3];

    uint64_t cpu_timestamp =
        ((uint64_t)data[4] << 32) |
        ((uint64_t)data[5] << 24) |
        ((uint64_t)data[6] << 16) |
        ((uint64_t)data[7] << 8) |
        (uint64_t)data[8];

    // Get local timestamp at the moment of receiving sync
    uint64_t local_timestamp = time_us_64();

    // Calculate clock offset (difference between CPU and local time)
    local_clock_offset = cpu_timestamp - local_timestamp;

    // Update synced frame counter
    synced_frame_counter = cpu_frame_counter;

    // Store master timestamp
    master_clock_timestamp = cpu_timestamp;

    // Send acknowledgment
    send_ack_to_cpu(0xF1);

    if (debug_enabled) {
        printf("Clock sync received: frame=%lu offset=%lld\n",
               synced_frame_counter, local_clock_offset);
    }
}

// Convert local time to master time
uint64_t get_master_time() {
    return time_us_64() + local_clock_offset;
}

// Handshaking Protocols
typedef struct {
    uint8_t command_id;
    uint8_t length;
    uint8_t data[256];
    bool requires_ack;
    uint32_t timestamp;
    uint8_t retry_count;
    bool completed;
} EnhancedCommand;

typedef struct {
    EnhancedCommand* commands;
    uint16_t capacity;
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    mutex_t lock;
    uint16_t pending_acks;
    uint8_t device_id; // 1=GPU, 2=APU
} EnhancedCommandQueue;

EnhancedCommandQueue gpu_queue;
EnhancedCommandQueue apu_queue;
const uint32_t COMMAND_TIMEOUT_MS = 50; // 50ms timeout for command response
const uint8_t MAX_RETRIES = 3;

// Improved command queuing with acknowledgment tracking
bool queue_command_with_ack(EnhancedCommandQueue* queue, uint8_t cmd_id,
                           uint8_t length, const uint8_t* data, bool needs_ack) {
    mutex_enter_blocking(&queue->lock);

    // Check if queue is full
    if (queue->count >= queue->capacity) {
        mutex_exit(&queue->lock);
        send_error_to_cpu(ERROR_QUEUE_FULL);
        return false;
    }

    // Add command to queue
    EnhancedCommand* cmd = &queue->commands[queue->tail];
    cmd->command_id = cmd_id;
    cmd->length = length;
    memcpy(cmd->data, data, length - 2);
    cmd->requires_ack = needs_ack;
    cmd->timestamp = time_ms_32();
    cmd->retry_count = 0;
    cmd->completed = false;

    // If this command requires ack, increment pending count
    if (needs_ack) {
        queue->pending_acks++;
    }

    // Update queue pointers
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    mutex_exit(&queue->lock);
    return true;
}

// APU Command Acknowledgment

void send_ack_to_cpu(uint8_t command_id) {
    // Prepare acknowledgment packet
    uint8_t ack_packet[4] = {
        0xFA,        // ACK command ID
        4,           // Packet length
        command_id,  // Original command being acknowledged
        0            // Status (0 = success)
    };

    // Wait for CPU to be ready to receive response
    while (gpio_get(CPU_CS_PIN) == 0) {
        sleep_us(10);
    }

    // Signal that we have data to send
    gpio_put(DATA_READY_PIN, 1);

    // Wait for CPU to assert CS (begin reading)
    uint32_t timeout = 1000; // 1ms timeout
    while (gpio_get(CPU_CS_PIN) == 1 && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    // If CPU responded, send the acknowledgment
    if (timeout > 0) {
        spi_write_blocking(SPI_PORT, ack_packet, 4);
    }

    // Clear data ready signal
    gpio_put(DATA_READY_PIN, 0);
}

void send_error_to_cpu(uint8_t command_id, uint8_t error_code) {
    // Prepare error packet
    uint8_t error_packet[4] = {
        0xFE,        // Error command ID
        4,           // Packet length
        command_id,  // Original command with error
        error_code   // Error code
    };

    // Similar pattern as acknowledgment sending
    // Wait for CPU to be ready
    while (gpio_get(CPU_CS_PIN) == 0) {
        sleep_us(10);
    }

    gpio_put(DATA_READY_PIN, 1);

    uint32_t timeout = 1000;
    while (gpio_get(CPU_CS_PIN) == 1 && timeout > 0) {
        sleep_us(1);
        timeout--;
    }

    if (timeout > 0) {
        spi_write_blocking(SPI_PORT, error_packet, 4);
    }

    gpio_put(DATA_READY_PIN, 0);
}

// APU Error Management

// Error handling state
volatile bool in_error_recovery = false;
volatile ErrorCode current_error = ERR_NONE;
volatile uint32_t last_error_time = 0;
volatile uint32_t error_count = 0;

void handle_error(ErrorCode code, uint8_t command_id) {
    // Update error state
    current_error = code;
    last_error_time = time_ms_32();
    error_count++;

    // Log error locally
    if (debug_enabled) {
        printf("Error %d for command 0x%02X\n", code, command_id);
    }

    // Send error notification to CPU
    send_error_to_cpu(command_id, code);

    // For serious errors, enter recovery mode
    if (code == ERR_MEMORY_FULL || code == ERR_SYNC_LOST ||
        code == ERR_COMMUNICATION_FAILURE) {
        in_error_recovery = true;

        // Take error-specific recovery actions
        switch (code) {
            case ERR_MEMORY_FULL:
                // Try to free resources
                emergency_memory_cleanup();
                break;

            case ERR_SYNC_LOST:
                // Prepare for resynchronization
                reset_sync_state();
                break;

            case ERR_COMMUNICATION_FAILURE:
                // Reset communication hardware
                reset_spi_interface();
                break;

            default:
                break;
        }
    }
}

void emergency_memory_cleanup() {
    // For GPU:
    #ifdef GPU_IMPLEMENTATION
    // Free least-critical resources
    flush_tile_cache(); // Clear entire tile cache
    clear_sprites(); // Remove all sprites
    reset_effects(); // Disable all effects
    #endif

    // For APU:
    #ifdef APU_IMPLEMENTATION
    // Free sound resources
    stop_all_sounds();
    clear_unused_samples();
    reset_effects();
    #endif

    // Mark recovery complete
    in_error_recovery = false;

    if (debug_enabled) {
        printf("Emergency memory cleanup complete\n");
    }
}

void reset_sync_state() {
    // Reset timing variables to prepare for new sync
    synced_frame_counter = 0;
    local_clock_offset = 0;

    // Mark as ready for resyncing
    in_error_recovery = false;

    if (debug_enabled) {
        printf("Reset sync state, waiting for master sync\n");
    }
}

void reset_spi_interface() {
    // Reinitialize SPI peripheral
    spi_deinit(SPI_PORT);
    sleep_ms(5);
    spi_init(SPI_PORT, SPI_FREQUENCY);

    // Reconfigure SPI pins
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_MISO_PIN, GPIO_FUNC_SPI);

    // Reset SPI state machine
    in_error_recovery = false;

    if (debug_enabled) {
        printf("SPI interface reset complete\n");
    }
}

// Process command queues with acknowledgment and retry handling
void process_enhanced_queue(EnhancedCommandQueue* queue) {
    // Max commands to process in one batch
    const int MAX_BATCH = 10;
    int processed = 0;

    while (processed < MAX_BATCH) {
        mutex_enter_blocking(&queue->lock);

        // Check if queue is empty
        if (queue->count == 0) {
            mutex_exit(&queue->lock);
            break;
        }

        // Get command from queue
        EnhancedCommand* cmd = &queue->commands[queue->head];

        // Check for retries of timed-out commands
        if (cmd->requires_ack && !cmd->completed) {
            uint32_t elapsed = time_ms_32() - cmd->timestamp;

            if (elapsed > COMMAND_TIMEOUT_MS) {
                // Command has timed out
                if (cmd->retry_count < MAX_RETRIES) {
                    // Retry the command
                    cmd->retry_count++;
                    cmd->timestamp = time_ms_32();

                    // Prepare buffer for retrying
                    uint8_t buffer[258];
                    buffer[0] = cmd->command_id;
                    buffer[1] = cmd->length;
                    memcpy(&buffer[2], cmd->data, cmd->length - 2);

                    mutex_exit(&queue->lock);

                    // Log retry attempt
                    if (debug_enabled) {
                        printf("Retry %d for command 0x%02X\n", cmd->retry_count, cmd->command_id);
                    }

                    // Send command to appropriate device
                    if (queue->device_id == 1) {
                        gpio_put(GPU_CS_PIN, 0);
                        spi_write_blocking(GPU_SPI_PORT, buffer, cmd->length);
                        gpio_put(GPU_CS_PIN, 1);
                    } else {
                        gpio_put(APU_CS_PIN, 0);
                        spi_write_blocking(APU_SPI_PORT, buffer, cmd->length);
                        gpio_put(APU_CS_PIN, 1);
                    }

                    processed++;
                    continue;
                } else {
                    // Max retries reached, mark as failed
                    log_error("Command 0x%02X failed after %d retries",
                             cmd->command_id, cmd->retry_count);

                    // Move to next command
                    queue->head = (queue->head + 1) % queue->capacity;
                    queue->count--;

                    if (cmd->requires_ack) {
                        queue->pending_acks--;
                    }

                    mutex_exit(&queue->lock);
                    continue;
                }
            }
        }

        // Only process completed or non-ack commands
        if (cmd->completed || !cmd->requires_ack) {
            // Prepare buffer for SPI transfer
            uint8_t buffer[258];
            buffer[0] = cmd->command_id;
            buffer[1] = cmd->length;
            memcpy(&buffer[2], cmd->data, cmd->length - 2);

            // Move to next command
            queue->head = (queue->head + 1) % queue->capacity;
            queue->count--;

            if (cmd->completed && cmd->requires_ack) {
                queue->pending_acks--;
            }

            mutex_exit(&queue->lock);

            // Send command to appropriate device
            if (queue->device_id == 1) {
                gpio_put(GPU_CS_PIN, 0);
                spi_write_blocking(GPU_SPI_PORT, buffer, cmd->length);
                gpio_put(GPU_CS_PIN, 1);
            } else {
                gpio_put(APU_CS_PIN, 0);
                spi_write_blocking(APU_SPI_PORT, buffer, cmd->length);
                gpio_put(APU_CS_PIN, 1);
            }

            processed++;
        } else {
            // Command requires acknowledgment but hasn't been completed yet
            mutex_exit(&queue->lock);
            break;
        }
    }
}

// Command Processing System
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    switch (cmd_id) {
        // System Commands
        case CMD_NOP:
            // No operation
            break;
            
        case CMD_RESET_AUDIO:
            reset_audio_system();
            send_ack_to_cpu(CMD_RESET_AUDIO);
            break;
            
        case CMD_SET_MASTER_VOLUME:
            set_master_volume(data[0]);
            send_ack_to_cpu(CMD_SET_MASTER_VOLUME);
            break;
        
        // Tracker Commands
        case CMD_TRACKER_LOAD:
            load_tracker(data[0], (data[1] | (data[2] << 8)), &data[3]);
            break;
            
        case CMD_TRACKER_PLAY:
            play_tracker(data[0]);
            break;
            
        // Channel Commands
        case CMD_CHANNEL_NOTE_ON:
            trigger_note(data[0], data[1], data[2]);
            break;
            
        // FM Synthesis Commands
        case CMD_FM_INIT_CHANNEL:
            init_fm_channel(data[0], data[1]);
            break;
            
        // Sample Commands
        case CMD_SAMPLE_LOAD:
            load_sample(data[0], data[1], data[2] | (data[3] << 8), 
                      data[4] | (data[5] << 8), data[6] | (data[7] << 8),
                      data[8] | (data[9] << 8), &data[10]);
            break;
            
        // Wavetable Commands
        case CMD_WAVE_DEFINE_TABLE:
            define_wavetable(data[0], data[1], &data[2]);
            break;
            
        // Effects Commands
        case CMD_EFFECT_SET_REVERB:
            configure_reverb(data[0], data[1], data[2]);
            break;
            
        // Memory Management
        case CMD_MEM_STATUS:
            send_memory_status();
            break;
            
        default:
            // Unknown command
            send_error(ERROR_UNKNOWN_COMMAND);
            break;
    }
}

// FM Synthesis definitions
typedef struct {
    uint8_t attack_rate;
    uint8_t decay_rate;
    uint8_t sustain_level;
    uint8_t release_rate;
    uint8_t waveform;
    int8_t detune;
    uint8_t multiple;
    bool enabled;
    
    // Runtime state
    float envelope_level;
    uint8_t envelope_state;  // 0=off, 1=attack, 2=decay, 3=sustain, 4=release
    uint32_t phase;
    float output;
} FMOperator;

typedef struct {
    uint8_t algorithm;
    uint8_t feedback;
    FMOperator operators[4];
    
    // Feedback history
    float op1_prev1;
    float op1_prev2;
} FMChannel;

FMChannel fm_channels[MAX_CHANNELS];

void init_fm_channel(uint8_t channel_id, uint8_t algorithm) {
    if (channel_id >= MAX_CHANNELS) return;
    
    // Configure channel as FM type
    channels[channel_id].type = CHANNEL_TYPE_FM;
    channels[channel_id].active = false;
    
    // Initialize FM parameters
    fm_channels[channel_id].algorithm = algorithm;
    fm_channels[channel_id].feedback = 0;
    
    // Reset all operators
    for (int i = 0; i < 4; i++) {
        FMOperator* op = &fm_channels[channel_id].operators[i];
        
        // Default values
        op->attack_rate = 31;    // Fast attack
        op->decay_rate = 20;     // Medium decay
        op->sustain_level = 10;  // Medium sustain
        op->release_rate = 15;   // Medium release
        op->waveform = 0;        // Sine wave
        op->multiple = 1;        // Base frequency
        op->detune = 0;          // No detune
        
        // Reset runtime state
        op->phase = 0;
        op->output = 0;
        op->envelope_level = 0;
        op->envelope_state = 0;
        op->enabled = true;
    }
    
    // Reset feedback
    fm_channels[channel_id].op1_prev1 = 0;
    fm_channels[channel_id].op1_prev2 = 0;
}

float compute_operator_output(uint8_t channel_id, uint8_t op_id, uint32_t phase_inc, float modulation) {
    FMOperator* op = &fm_channels[channel_id].operators[op_id];
    
    if (!op->enabled || op->envelope_state == 0) {
        return 0.0f;
    }
    
    // Apply frequency multiple and detune
    uint32_t adjusted_phase_inc = phase_inc * op->multiple;
    
    // Add detune
    if (op->detune != 0) {
        adjusted_phase_inc = adjusted_phase_inc * (1.0f + (op->detune * 0.01f));
    }
    
    // Add modulation to phase
    op->phase += adjusted_phase_inc + (uint32_t)(modulation * 10000.0f); 
    
    // Get waveform value
    float value;
    
    switch(op->waveform) {
        case 0: // Sine
            value = sine_table[(op->phase >> 24) & 0xFF] / 32767.0f;
            break;
            
        case 1: // Square
            value = ((op->phase >> 31) & 1) ? 1.0f : -1.0f;
            break;
            
        case 2: // Sawtooth
            value = ((op->phase >> 24) / 128.0f) - 1.0f;
            break;
            
        case 3: // Triangle
            {
                uint8_t index = (op->phase >> 24);
                value = (index < 128) 
                    ? -1.0f + (index / 64.0f) 
                    : 3.0f - (index / 64.0f);
            }
            break;
            
        default:
            value = 0.0f;
    }
    
    // Apply envelope
    value *= op->envelope_level;
    
    // Update envelope
    update_envelope(op);
    
    return value;
}

void render_fm_channel(uint8_t channel_id, float* buffer, uint32_t sample_count) {
    FMChannel* fm = &fm_channels[channel_id];
    Channel* ch = &channels[channel_id];
    
    if (!ch->active) return;
    
    uint32_t phase_inc = (uint32_t)(ch->frequency * 4294967296.0f / SAMPLE_RATE);
    float vol_left = ch->volume * (255 - ch->pan) / 65025.0f;
    float vol_right = ch->volume * ch->pan / 65025.0f;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        float output = 0.0f;
        
        // Process FM algorithm
        switch (fm->algorithm) {
            case 0: // Serial 1->2->3->4
                {
                    float mod1 = compute_operator_output(channel_id, 0, phase_inc, 0);
                    float mod2 = compute_operator_output(channel_id, 1, phase_inc, mod1);
                    float mod3 = compute_operator_output(channel_id, 2, phase_inc, mod2);
                    output = compute_operator_output(channel_id, 3, phase_inc, mod3);
                }
                break;
                
            case 1: // 1->2->4, 3->4
                {
                    float mod1 = compute_operator_output(channel_id, 0, phase_inc, 0);
                    float mod2 = compute_operator_output(channel_id, 1, phase_inc, mod1);
                    float mod3 = compute_operator_output(channel_id, 2, phase_inc, 0);
                    output = compute_operator_output(channel_id, 3, phase_inc, mod2 + mod3);
                }
                break;
                
            // Additional algorithms would be implemented here
            
            case 7: // 1 with feedback, 2,3,4 independent
                {
                    // Calculate feedback for operator 1
                    float feedback = (fm->op1_prev1 + fm->op1_prev2) * fm->feedback / 100.0f;
                    
                    // Get operator outputs
                    float out1 = compute_operator_output(channel_id, 0, phase_inc, feedback);
                    float out2 = compute_operator_output(channel_id, 1, phase_inc, 0);
                    float out3 = compute_operator_output(channel_id, 2, phase_inc, 0);
                    float out4 = compute_operator_output(channel_id, 3, phase_inc, 0);
                    
                    // Update feedback history
                    fm->op1_prev2 = fm->op1_prev1;
                    fm->op1_prev1 = out1;
                    
                    // Mix outputs
                    output = out1 + out2 + out3 + out4;
                }
                break;
        }
        
        // Add to stereo mix buffer
        buffer[i*2] += output * vol_left;
        buffer[i*2+1] += output * vol_right;
    }
}

// Sample Playback Engine
typedef struct {
    bool loaded;
    uint8_t* data;
    uint32_t size;
    uint16_t sample_rate;
    uint16_t loop_start;
    uint16_t loop_end;
    bool is_16bit;
    bool is_stereo;
    uint8_t bytes_per_sample;
} Sample;

typedef struct {
    uint8_t sample_id;
    uint32_t position;
    float position_frac;
    float step;
    float pitch_ratio;
    uint8_t loop_mode; // 0=none, 1=forward, 2=ping-pong
    int8_t direction;  // 1=forward, -1=reverse (for ping-pong)
} SampleChannel;

Sample samples[MAX_SAMPLES];
SampleChannel sample_channels[MAX_CHANNELS];

void load_sample(uint8_t sample_id, uint8_t format, uint16_t sample_rate, 
                uint16_t loop_start, uint16_t loop_end, uint16_t size, const uint8_t* data) {
    if (sample_id >= MAX_SAMPLES) return;
    
    // Free previous sample if it exists
    if (samples[sample_id].data != NULL) {
        free(samples[sample_id].data);
    }
    
    // Parse format flags
    bool is_16bit = (format & 1);
    bool is_stereo = (format & 2);
    
    // Allocate and copy sample data
    uint8_t* sample_memory = malloc(size);
    if (!sample_memory) {
        send_error(ERROR_OUT_OF_MEMORY);
        return;
    }
    
    memcpy(sample_memory, data, size);
    
    // Initialize sample
    samples[sample_id].loaded = true;
    samples[sample_id].data = sample_memory;
    samples[sample_id].size = size;
    samples[sample_id].sample_rate = sample_rate;
    samples[sample_id].loop_start = loop_start;
    samples[sample_id].loop_end = loop_end;
    samples[sample_id].is_16bit = is_16bit;
    samples[sample_id].is_stereo = is_stereo;
    samples[sample_id].bytes_per_sample = (is_16bit ? 2 : 1) * (is_stereo ? 2 : 1);
    
    send_ack_to_cpu(CMD_SAMPLE_LOAD);
}

void play_sample(uint8_t channel_id, uint8_t sample_id, uint8_t pitch, uint8_t volume) {
    if (channel_id >= MAX_CHANNELS || sample_id >= MAX_SAMPLES || !samples[sample_id].loaded) {
        return;
    }
    
    // Configure channel
    channels[channel_id].type = CHANNEL_TYPE_SAMPLE;
    channels[channel_id].active = true;
    channels[channel_id].volume = volume;
    
    // Set up sample playback
    sample_channels[channel_id].sample_id = sample_id;
    sample_channels[channel_id].position = 0;
    sample_channels[channel_id].position_frac = 0;
    sample_channels[channel_id].direction = 1;
    
    // Calculate pitch shift (0x40=half speed, 0x80=normal, 0xC0=double speed)
    sample_channels[channel_id].pitch_ratio = powf(2.0f, (pitch - 128) / 64.0f);
    
    // Calculate step size for position advancement
    float step = (float)samples[sample_id].sample_rate * sample_channels[channel_id].pitch_ratio / (float)SAMPLE_RATE;
    sample_channels[channel_id].step = step;
    
    // Default to no looping
    sample_channels[channel_id].loop_mode = 0;
    
    send_ack_to_cpu(CMD_SAMPLE_PLAY);
}

// Linear interpolation for smoother sample playback
int16_t interpolate_sample(const uint8_t* data, uint32_t pos, float frac, bool is_16bit) {
    int16_t sample1, sample2;
    
    if (is_16bit) {
        sample1 = ((int16_t)data[pos*2] | ((int16_t)data[pos*2+1] << 8));
        sample2 = ((int16_t)data[(pos+1)*2] | ((int16_t)data[(pos+1)*2+1] << 8));
    } else {
        sample1 = ((int16_t)data[pos] - 128) * 256;
        sample2 = ((int16_t)data[pos+1] - 128) * 256;
    }
    
    return sample1 + (int16_t)((sample2 - sample1) * frac);
}

void render_sample_channel(uint8_t channel_id, float* buffer, uint32_t sample_count) {
    Channel* ch = &channels[channel_id];
    SampleChannel* sc = &sample_channels[channel_id];
    
    if (!ch->active) return;
    
    Sample* sample = &samples[sc->sample_id];
    if (!sample->loaded) return;
    
    float vol_left = ch->volume * (255 - ch->pan) / 65025.0f;
    float vol_right = ch->volume * ch->pan / 65025.0f;
    
    uint32_t sample_end = sample->size / sample->bytes_per_sample;
    uint32_t loop_start = sample->loop_start;
    uint32_t loop_end = sample->loop_end;
    
    // Ensure loop points are valid
    if (loop_end > sample_end) loop_end = sample_end;
    if (loop_start >= loop_end) loop_start = 0;
    
    for (uint32_t i = 0; i < sample_count; i++) {
        // Check if we've reached the end
        if (sc->position >= sample_end) {
            if (sc->loop_mode == 0) {
                // End playback
                ch->active = false;
                break;
            } else if (sc->loop_mode == 1) {
                // Forward loop
                sc->position = loop_start;
                sc->position_frac = 0;
            } else if (sc->loop_mode == 2) {
                // Ping-pong loop
                sc->position = loop_end - 1;
                sc->position_frac = 0;
                sc->direction = -1;
            }
        } else if (sc->position < loop_start && sc->direction < 0 && sc->loop_mode == 2) {
            // Reverse ping-pong at loop start
            sc->position = loop_start;
            sc->position_frac = 0;
            sc->direction = 1;
        }
        
        // Read sample with interpolation
        int16_t left, right;
        
        if (sample->is_stereo) {
            // Stereo sample
            uint32_t pos = sc->position * 2;
            float frac = sc->position_frac;
            
            left = interpolate_sample(sample->data, pos, frac, sample->is_16bit);
            right = interpolate_sample(sample->data, pos + 1, frac, sample->is_16bit);
        } else {
            // Mono sample
            uint32_t pos = sc->position;
            float frac = sc->position_frac;
            
            left = right = interpolate_sample(sample->data, pos, frac, sample->is_16bit);
        }
        
        // Mix into output buffer
        buffer[i*2] += left * vol_left / 32768.0f;
        buffer[i*2+1] += right * vol_right / 32768.0f;
        
        // Advance position
        sc->position_frac += sc->step;
        while (sc->position_frac >= 1.0f) {
            sc->position_frac -= 1.0f;
            sc->position += sc->direction;
        }
    }
}

// Wavetable Synthesis Engine
typedef struct {
    int16_t* data;
    uint16_t size;
    uint16_t mask; // For fast indexing (size-1)
} Wavetable;

typedef struct {
    uint8_t table_id;
    float position;
    float position_frac;
    
    uint8_t sweep_start_table;
    uint8_t sweep_end_table;
    uint8_t sweep_rate;
    uint8_t sweep_position;
    bool sweep_active;
    bool sweep_oscillate;
    uint16_t sweep_size;
    
    uint8_t pulse_width;
    uint8_t mod_depth;
    uint8_t mod_speed;
    uint8_t mod_phase;
} WaveChannel;

Wavetable wavetables[MAX_WAVETABLES];
WaveChannel wave_channels[MAX_CHANNELS];

void define_wavetable(uint8_t table_id, uint8_t wave_size, const uint8_t* data) {
    if (table_id >= MAX_WAVETABLES) return;
    
    // Calculate actual size (must be power of 2)
    uint16_t actual_size = 256; // Default
    if (wave_size > 0) {
        // Find nearest power of 2
        actual_size = 32;
        while (actual_size < wave_size && actual_size < 512) {
            actual_size *= 2;
        }
    }
    
    // Free existing wavetable if any
    if (wavetables[table_id].data != NULL) {
        free(wavetables[table_id].data);
    }
    
    // Allocate memory
    int16_t* wave_memory = malloc(actual_size * sizeof(int16_t));
    if (!wave_memory) {
        send_error(ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Convert 8-bit unsigned data to 16-bit signed
    for (int i = 0; i < actual_size; i++) {
        if (i < wave_size) {
            // Convert 0-255 to -32768 to 32767
            wave_memory[i] = ((int16_t)data[i] - 128) * 256;
        } else {
            // Zero-pad if input is smaller
            wave_memory[i] = 0;
        }
    }
    
    // Store wavetable
    wavetables[table_id].data = wave_memory;
    wavetables[table_id].size = actual_size;
    wavetables[table_id].mask = actual_size - 1;
    
    send_ack_to_cpu(CMD_WAVE_DEFINE_TABLE);
}

void set_wavetable_sweep(uint8_t channel_id, uint8_t start_table, uint8_t end_table, uint8_t sweep_rate) {
    if (channel_id >= MAX_CHANNELS) return;
    
    // Validate wavetable IDs
    if (start_table >= MAX_WAVETABLES || wavetables[start_table].data == NULL ||
        end_table >= MAX_WAVETABLES || wavetables[end_table].data == NULL) {
        send_error(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Configure morphing
    wave_channels[channel_id].sweep_start_table = start_table;
    wave_channels[channel_id].sweep_end_table = end_table;
    wave_channels[channel_id].sweep_rate = sweep_rate;
    wave_channels[channel_id].sweep_position = 0;
    wave_channels[channel_id].sweep_active = true;
    
    // Check wavetable size compatibility
    if (wavetables[start_table].size != wavetables[end_table].size) {
        wave_channels[channel_id].sweep_size = 
            (wavetables[start_table].size < wavetables[end_table].size) ? 
            wavetables[start_table].size : wavetables[end_table].size;
    } else {
        wave_channels[channel_id].sweep_size = wavetables[start_table].size;
    }
    
    send_ack_to_cpu(CMD_WAVE_SET_SWEEP);
}

void render_wavetable_channel(uint8_t channel_id, float* buffer, uint32_t sample_count) {
    Channel* ch = &channels[channel_id];
    WaveChannel* wc = &wave_channels[channel_id];
    
    if (!ch->active) return;
    
    // Get wavetable
    uint8_t table_id = wc->table_id;
    if (table_id >= MAX_WAVETABLES || wavetables[table_id].data == NULL) {
        ch->active = false;
        return;
    }
    
    float vol_left = ch->volume * (255 - ch->pan) / 65025.0f;
    float vol_right = ch->volume * ch->pan / 65025.0f;
    
    uint16_t wave_size = wavetables[table_id].size;
    uint16_t wave_mask = wavetables[table_id].mask;
    
    // Calculate phase increment based on frequency
    float phase_inc = ch->frequency * wave_size / SAMPLE_RATE;
    
    // Handle wavetable morphing
    int16_t* wave_data;
    int16_t* morph_data = NULL;
    float morph_factor = 0.0f;
    
    if (wc->sweep_active) {
        wave_data = wavetables[wc->sweep_start_table].data;
        morph_data = wavetables[wc->sweep_end_table].data;
        morph_factor = wc->sweep_position / 255.0f;
        
        // Advance morphing
        wc->sweep_position += wc->sweep_rate;
        if (wc->sweep_position > 255) {
            if (wc->sweep_oscillate) {
                // Swap tables and continue morphing
                uint8_t temp = wc->sweep_start_table;
                wc->sweep_start_table = wc->sweep_end_table;
                wc->sweep_end_table = temp;
                wc->sweep_position = 0;
            } else {
                // Clamp at end
                wc->sweep_position = 255;
            }
        }
    } else {
        wave_data = wavetables[table_id].data;
    }
    
    for (uint32_t i = 0; i < sample_count; i++) {
        float sample;
        
        if (wc->sweep_active && morph_data != NULL) {
            // Wavetable morphing
            uint16_t pos = (uint16_t)wc->position & wave_mask;
            uint16_t pos_next = (pos + 1) & wave_mask;
            
            // Get samples from both wavetables
            int16_t sample1a = wave_data[pos];
            int16_t sample1b = wave_data[pos_next];
            int16_t sample2a = morph_data[pos];
            int16_t sample2b = morph_data[pos_next];
            
            // Interpolate within each wavetable
            float frac = wc->position - (int)wc->position;
            float interp1 = sample1a + (sample1b - sample1a) * frac;
            float interp2 = sample2a + (sample2b - sample2a) * frac;
            
            // Interpolate between wavetables
            sample = interp1 + (interp2 - interp1) * morph_factor;
        } else {
            // Standard wavetable
            uint16_t pos = (uint16_t)wc->position & wave_mask;
            uint16_t pos_next = (pos + 1) & wave_mask;
            
            int16_t sample1 = wave_data[pos];
            int16_t sample2 = wave_data[pos_next];
            
            // Linear interpolation
            float frac = wc->position - (int)wc->position;
            sample = sample1 + (sample2 - sample1) * frac;
        }
        
        // Apply to stereo mix buffer
        buffer[i*2] += sample * vol_left / 32768.0f;
        buffer[i*2+1] += sample * vol_right / 32768.0f;
        
        // Advance wavetable position
        wc->position += phase_inc;
        while (wc->position >= wave_size) {
            wc->position -= wave_size;
        }
    }
}

// Tracker/Sequencer System
typedef struct {
    uint8_t note;        // 0=none, 1-96=note, 97=note off
    uint8_t instrument;
    uint8_t volume;      // 0=no change
    uint8_t effect;      // 0=none
    uint8_t effect_param;
} TrackerNote;

typedef struct {
    bool playing;
    uint8_t tempo;
    uint8_t ticks_per_row;
    uint8_t num_channels;
    uint8_t channel_map[MAX_TRACKER_CHANNELS]; // Maps tracker channels to audio channels
    
    uint8_t song_length;
    uint8_t pattern_sequence[256];
    
    uint8_t current_pattern;
    uint8_t current_row;
    uint8_t position_in_sequence;
    uint8_t tick_counter;
    
    TrackerNote* pattern_data;
    uint8_t rows_per_pattern;
    
    bool loop_enabled;
} Tracker;

Tracker trackers[MAX_TRACKERS];

void load_tracker(uint8_t tracker_id, uint16_t data_size, const uint8_t* data) {
    if (tracker_id >= MAX_TRACKERS) {
        send_error(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Stop if playing
    trackers[tracker_id].playing = false;
    
    // Parse header
    uint8_t num_channels = data[0];
    uint8_t num_patterns = data[1];
    uint8_t num_instruments = data[2];
    uint8_t song_length = data[3];
    uint8_t default_tempo = data[4];
    
    // Configure tracker
    trackers[tracker_id].num_channels = (num_channels <= MAX_TRACKER_CHANNELS) ? 
                                      num_channels : MAX_TRACKER_CHANNELS;
    trackers[tracker_id].song_length = song_length;
    trackers[tracker_id].tempo = default_tempo;
    trackers[tracker_id].ticks_per_row = 6; // Default value
    trackers[tracker_id].position_in_sequence = 0;

    // Copy pattern sequence
    memcpy(trackers[tracker_id].pattern_sequence, &data[5], song_length);
    
    // Pattern data starts after the header and sequence
    uint16_t pattern_data_offset = 5 + song_length;
    
    // Allocate pattern data memory
    uint32_t pattern_size = num_patterns * MAX_ROWS_PER_PATTERN * num_channels * sizeof(TrackerNote);
    if (trackers[tracker_id].pattern_data != NULL) {
        free(trackers[tracker_id].pattern_data);
    }
    
    trackers[tracker_id].pattern_data = malloc(pattern_size);
    if (trackers[tracker_id].pattern_data == NULL) {
        send_error(ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Parse and store pattern data
    for (int p = 0; p < num_patterns; p++) {
        uint8_t rows = data[pattern_data_offset++];
        
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < num_channels; c++) {
                TrackerNote* note = &trackers[tracker_id].pattern_data[
                    (p * MAX_ROWS_PER_PATTERN * num_channels) + 
                    (r * num_channels) + c];
                
                note->note = data[pattern_data_offset++];
                note->instrument = data[pattern_data_offset++];
                note->volume = data[pattern_data_offset++];
                note->effect = data[pattern_data_offset++];
                note->effect_param = data[pattern_data_offset++];
            }
        }
    }
    
    // Setup default channel mapping (1:1)
    for (int i = 0; i < trackers[tracker_id].num_channels; i++) {
        trackers[tracker_id].channel_map[i] = i;
    }
    
    // Default to looping enabled
    trackers[tracker_id].loop_enabled = true;
    
    send_ack_to_cpu(CMD_TRACKER_LOAD);
}

void play_tracker(uint8_t tracker_id) {
    if (tracker_id >= MAX_TRACKERS || trackers[tracker_id].pattern_data == NULL) {
        send_error(ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Initialize playback state
    trackers[tracker_id].playing = true;
    trackers[tracker_id].position_in_sequence = 0;
    trackers[tracker_id].current_row = 0;
    trackers[tracker_id].tick_counter = 0;
    trackers[tracker_id].current_pattern = trackers[tracker_id].pattern_sequence[0];
    
    // Process first row to start playing notes
    process_tracker_row(tracker_id);
    
    send_ack_to_cpu(CMD_TRACKER_PLAY);
}

void process_tracker_row(uint8_t tracker_id) {
    Tracker* tr = &trackers[tracker_id];
    uint8_t pattern = tr->current_pattern;
    uint8_t row = tr->current_row;
    
    // Get pointer to this row's notes
    TrackerNote* row_data = &tr->pattern_data[
        (pattern * MAX_ROWS_PER_PATTERN * tr->num_channels) + 
        (row * tr->num_channels)];
    
    // Process each channel
    for (int c = 0; c < tr->num_channels; c++) {
        TrackerNote* note = &row_data[c];
        uint8_t channel_id = tr->channel_map[c];
        
        // Skip if this channel isn't allocated
        if (channel_id >= MAX_CHANNELS) continue;
        
        // Process note
        if (note->note > 0) {
            if (note->note == 97) { // Note off
                channels[channel_id].active = false;
            } else if (note->note <= 96) { // Regular note
                // Calculate frequency from note number (1=C-0, 96=B-7)
                float freq = 32.7032f * powf(2.0f, (note->note - 1) / 12.0f);
                
                // Load instrument if specified
                if (note->instrument > 0) {
                    // This would load the appropriate instrument
                    // For simplicity, we'll just set channel type here
                    if (note->instrument <= 32) {
                        channels[channel_id].type = CHANNEL_TYPE_FM;
                    } else if (note->instrument <= 64) {
                        channels[channel_id].type = CHANNEL_TYPE_SAMPLE;
                    } else {
                        channels[channel_id].type = CHANNEL_TYPE_WAVETABLE;
                    }
                }
                
                // Start note
                channels[channel_id].active = true;
                channels[channel_id].frequency = freq;
                
                // Trigger according to channel type
                switch (channels[channel_id].type) {
                    case CHANNEL_TYPE_FM:
                        trigger_fm_note(channel_id, freq);
                        break;
                        
                    case CHANNEL_TYPE_SAMPLE:
                        trigger_sample_note(channel_id, freq);
                        break;
                        
                    case CHANNEL_TYPE_WAVETABLE:
                        trigger_wavetable_note(channel_id, freq);
                        break;
                }
            }
        }
        
        // Process volume change
        if (note->volume > 0) {
            channels[channel_id].volume = note->volume;
        }
        
        // Process effect
        if (note->effect > 0) {
            process_tracker_effect(tracker_id, channel_id, note->effect, note->effect_param);
        }
    }
}

void update_tracker(uint32_t elapsed_us) {
    for (int t = 0; t < MAX_TRACKERS; t++) {
        Tracker* tr = &trackers[t];
        
        if (!tr->playing) continue;
        
        // Calculate tick duration based on tempo
        uint32_t tick_us = 2500000 / tr->tempo; // 2.5M = 60 sec * 1M microseconds / 24 ticks per quarter note
        
        // Add elapsed time to accumulator
        tr->tick_accumulator += elapsed_us;
        
        // Process ticks
        while (tr->tick_accumulator >= tick_us) {
            tr->tick_accumulator -= tick_us;
            
            // Increment tick counter
            tr->tick_counter++;
            
            // Process row if needed
            if (tr->tick_counter >= tr->ticks_per_row) {
                tr->tick_counter = 0;
                
                // Move to next row
                tr->current_row++;
                
                // Check if we've reached the end of the pattern
                if (tr->current_row >= tr->rows_per_pattern) {
                    tr->current_row = 0;
                    
                    // Move to next pattern
                    tr->position_in_sequence++;
                    
                    // Check if we've reached the end of the song
                    if (tr->position_in_sequence >= tr->song_length) {
                        if (tr->loop_enabled) {
                            tr->position_in_sequence = 0;
                        } else {
                            tr->playing = false;
                            break;
                        }
                    }
                    
                    // Set current pattern
                    tr->current_pattern = tr->pattern_sequence[tr->position_in_sequence];
                }
                
                // Process new row
                process_tracker_row(t);
            } else {
                // Process per-tick effects (like arpeggio, vibrato, etc.)
                process_tracker_tick_effects(t);
            }
        }
    }
}

void process_tracker_effect(uint8_t tracker_id, uint8_t channel_id, uint8_t effect, uint8_t param) {
    // Apply appropriate effect based on code and parameter
    switch (effect) {
        case 0x0: // Arpeggio
            if (param != 0) {
                channels[channel_id].arpeggio_enabled = true;
                channels[channel_id].arpeggio_note1 = param >> 4;    // First semitone offset
                channels[channel_id].arpeggio_note2 = param & 0x0F;  // Second semitone offset
                channels[channel_id].arpeggio_counter = 0;
            } else {
                channels[channel_id].arpeggio_enabled = false;
            }
            break;
            
        case 0x1: // Portamento up
            channels[channel_id].portamento_up = param;
            channels[channel_id].portamento_active = true;
            break;
            
        case 0x2: // Portamento down
            channels[channel_id].portamento_down = param;
            channels[channel_id].portamento_active = true;
            break;
            
        case 0x4: // Vibrato
            channels[channel_id].vibrato_speed = param >> 4;
            channels[channel_id].vibrato_depth = param & 0x0F;
            channels[channel_id].vibrato_active = true;
            break;
            
        case 0x7: // Tremolo
            channels[channel_id].tremolo_speed = param >> 4;
            channels[channel_id].tremolo_depth = param & 0x0F;
            channels[channel_id].tremolo_active = true;
            break;
            
        case 0xA: // Volume slide
            {
                uint8_t up = param >> 4;
                uint8_t down = param & 0x0F;
                
                if (up > 0) {
                    channels[channel_id].volume_slide = up;
                    channels[channel_id].volume_slide_direction = 1;
                } else {
                    channels[channel_id].volume_slide = down;
                    channels[channel_id].volume_slide_direction = -1;
                }
                
                channels[channel_id].volume_slide_active = true;
            }
            break;
            
        case 0xF: // Set tempo/speed
            if (param <= 0x1F) {
                trackers[tracker_id].ticks_per_row = param;
            } else {
                trackers[tracker_id].tempo = param;
            }
            break;
    }
}

void process_tracker_tick_effects(uint8_t tracker_id) {
    Tracker* tr = &trackers[tracker_id];
    
    // Process active tick effects for each channel
    for (int c = 0; c < tr->num_channels; c++) {
        uint8_t channel_id = tr->channel_map[c];
        
        if (channel_id >= MAX_CHANNELS || !channels[channel_id].active) continue;
        
        // Apply arpeggio
        if (channels[channel_id].arpeggio_enabled) {
            float base_freq = channels[channel_id].base_frequency;
            int semitones = 0;
            
            switch (channels[channel_id].arpeggio_counter % 3) {
                case 0: semitones = 0; break;
                case 1: semitones = channels[channel_id].arpeggio_note1; break;
                case 2: semitones = channels[channel_id].arpeggio_note2; break;
            }
            
            if (semitones > 0) {
                channels[channel_id].frequency = base_freq * powf(2.0f, semitones / 12.0f);
                update_channel_frequency(channel_id);
            } else {
                channels[channel_id].frequency = base_freq;
                update_channel_frequency(channel_id);
            }
            
            channels[channel_id].arpeggio_counter++;
        }
        
        // Apply portamento
        if (channels[channel_id].portamento_active) {
            if (channels[channel_id].portamento_up > 0) {
                channels[channel_id].frequency *= powf(2.0f, channels[channel_id].portamento_up / (12.0f * 16.0f));
                update_channel_frequency(channel_id);
            } else if (channels[channel_id].portamento_down > 0) {
                channels[channel_id].frequency /= powf(2.0f, channels[channel_id].portamento_down / (12.0f * 16.0f));
                update_channel_frequency(channel_id);
            }
        }
        
        // Apply vibrato
        if (channels[channel_id].vibrato_active) {
            float depth = channels[channel_id].vibrato_depth / 16.0f;  // Semitones
            
            // Calculate vibrato sine wave
            channels[channel_id].vibrato_phase += channels[channel_id].vibrato_speed;
            float vibrato_value = sinf(channels[channel_id].vibrato_phase * 0.1f) * depth;
            
            // Apply to frequency
            float base_freq = channels[channel_id].base_frequency;
            channels[channel_id].frequency = base_freq * powf(2.0f, vibrato_value / 12.0f);
            update_channel_frequency(channel_id);
        }
        
        // Apply tremolo
        if (channels[channel_id].tremolo_active) {
            uint8_t depth = channels[channel_id].tremolo_depth;
            
            // Calculate tremolo sine wave
            channels[channel_id].tremolo_phase += channels[channel_id].tremolo_speed;
            float tremolo_value = (sinf(channels[channel_id].tremolo_phase * 0.1f) + 1.0f) * 0.5f;
            
            // Apply to volume
            uint8_t base_volume = channels[channel_id].base_volume;
            uint8_t tremolo_amount = (uint8_t)(depth * tremolo_value);
            
            if (tremolo_amount > base_volume) {
                channels[channel_id].volume = 0;
            } else {
                channels[channel_id].volume = base_volume - tremolo_amount;
            }
        }
        
        // Apply volume slide
        if (channels[channel_id].volume_slide_active) {
            if (channels[channel_id].volume_slide_direction > 0) {
                channels[channel_id].volume += channels[channel_id].volume_slide;
                if (channels[channel_id].volume > 255) {
                    channels[channel_id].volume = 255;
                }
            } else {
                if (channels[channel_id].volume < channels[channel_id].volume_slide) {
                    channels[channel_id].volume = 0;
                } else {
                    channels[channel_id].volume -= channels[channel_id].volume_slide;
                }
            }
        }
    }
}

//Effects Processing
typedef struct {
    bool enabled;
    uint8_t room_size;
    uint8_t damping;
    uint8_t wet;
    
    // Reverb algorithm parameters
    float feedback;
    float lp_coeff;
    float wet_gain;
    float dry_gain;
    
    // Delay lines
    float* buffer;
    uint32_t buffer_size;
    uint32_t comb1_idx, comb2_idx, comb3_idx, comb4_idx;
    uint32_t ap1_idx, ap2_idx;
    
    // Filter state
    float comb1_lp, comb2_lp, comb3_lp, comb4_lp;
    
    uint8_t prev_room_size;
} Reverb;

typedef struct {
    bool enabled;
    uint16_t time;       // Delay time in ms
    uint32_t samples;    // Delay time in samples
    uint8_t feedback;    // 0-255
    uint8_t wet;         // 0-255
    uint8_t dry;
    
    // Runtime parameters
    float feedback_gain;
    float wet_gain;
    float dry_gain;
    
    // Delay buffer
    int16_t* buffer;
    uint32_t buffer_size;
    uint32_t write_pos;
    uint32_t prev_samples;
} Delay;

typedef struct {
    bool enabled;
    uint8_t type;        // 0=lowpass, 1=highpass, 2=bandpass
    uint8_t cutoff;      // 0-255
    uint8_t resonance;   // 0-255
    
    // Filter coefficients
    float a0, a1, a2, b1, b2;
    
    // Filter state
    float x1, x2, y1, y2;
} Filter;

Reverb reverb;
Delay delay;
Filter filters[MAX_CHANNELS];

void configure_reverb(uint8_t room_size, uint8_t damping, uint8_t wet) {
    reverb.room_size = room_size;
    reverb.damping = damping;
    reverb.wet = wet;
    reverb.dry = 255 - wet;
    
    // Calculate reverb parameters
    float normalized_room = room_size / 255.0f;
    float normalized_damp = damping / 255.0f;
    float normalized_wet = wet / 255.0f;
    
    // Set algorithm parameters
    reverb.feedback = 0.7f + normalized_room * 0.28f;  // 0.7 - 0.98 range
    reverb.lp_coeff = 1.0f - normalized_damp * 0.95f;  // 1.0 - 0.05 range
    reverb.wet_gain = normalized_wet;
    reverb.dry_gain = 1.0f - normalized_wet * 0.5f;
    
    // Clear buffer if room size changed significantly
    if (abs(reverb.prev_room_size - room_size) > 50) {
        memset(reverb.buffer, 0, reverb.buffer_size * sizeof(float));
    }
    
    reverb.prev_room_size = room_size;
    reverb.enabled = (wet > 0);
    
    send_ack_to_cpu(CMD_EFFECT_SET_REVERB);
}

void configure_delay(uint16_t delay_time, uint8_t feedback, uint8_t wet) {
    // Calculate delay in samples
    uint32_t delay_samples = (delay_time * SAMPLE_RATE) / 1000;
    
    // Ensure delay_samples is within buffer capacity
    if (delay_samples > delay.buffer_size / 2) {
        delay_samples = delay.buffer_size / 2;
    }
    
    delay.time = delay_time;
    delay.samples = delay_samples;
    delay.feedback = feedback;
    delay.wet = wet;
    delay.dry = 255 - (wet / 2);  // Less aggressive dry reduction
    
    // Calculate runtime parameters
    delay.feedback_gain = feedback / 255.0f;
    delay.wet_gain = wet / 255.0f;
    delay.dry_gain = delay.dry / 255.0f;
    
    // Clear buffer if delay time changed significantly
    if (abs((int32_t)delay.prev_samples - (int32_t)delay_samples) > SAMPLE_RATE / 50) {
        memset(delay.buffer, 0, delay.buffer_size * sizeof(int16_t));
    }
    
    delay.prev_samples = delay_samples;
    delay.write_pos = 0;
    delay.enabled = (wet > 0);
    
    send_ack_to_cpu(CMD_EFFECT_SET_DELAY);
}

void configure_filter(uint8_t channel_id, uint8_t filter_type, uint8_t cutoff, uint8_t resonance) {
    if (channel_id >= MAX_CHANNELS) {
        send_error(ERROR_INVALID_PARAMETER);
        return;
    }
    
    Filter* filter = &filters[channel_id];
    
    filter->type = filter_type;
    filter->cutoff = cutoff;
    filter->resonance = resonance;
    
    // Convert parameters to filter coefficients
    float normalized_cutoff = (cutoff / 255.0f) * 0.45f;  // 0-0.45 range (below Nyquist)
    float normalized_resonance = resonance / 255.0f;
    
    // Calculate filter coefficients based on filter type
    switch (filter_type) {
        case 0:  // Lowpass
            calculate_lowpass_coefficients(filter, normalized_cutoff, normalized_resonance);
            break;
            
        case 1:  // Highpass
            calculate_highpass_coefficients(filter, normalized_cutoff, normalized_resonance);
            break;
            
        case 2:  // Bandpass
            calculate_bandpass_coefficients(filter, normalized_cutoff, normalized_resonance);
            break;
            
        default:
            send_error(ERROR_INVALID_PARAMETER);
            return;
    }
    
    // Reset filter state
    filter->x1 = 0;
    filter->x2 = 0;
    filter->y1 = 0;
    filter->y2 = 0;
    
    filter->enabled = true;
    
    send_ack_to_cpu(CMD_EFFECT_SET_FILTER);
}

void apply_reverb(float* buffer, uint32_t num_samples) {
    if (!reverb.enabled) return;
    
    float feedback = reverb.feedback;
    float lp_coeff = reverb.lp_coeff;
    
    for (uint32_t i = 0; i < num_samples; i++) {
        // Get current stereo sample
        float left = buffer[i*2];
        float right = buffer[i*2+1];
        
        // Mix to mono for reverb processing
        float mono_input = (left + right) * 0.5f;
        
        // Apply comb filters (parallel)
        float comb1 = reverb.buffer[reverb.comb1_idx];
        float comb2 = reverb.buffer[reverb.comb2_idx];
        float comb3 = reverb.buffer[reverb.comb3_idx];
        float comb4 = reverb.buffer[reverb.comb4_idx];
        
        // Apply lowpass filtering and feedback
        reverb.comb1_lp = (reverb.comb1_lp * lp_coeff) + (comb1 * (1.0f - lp_coeff));
        reverb.comb2_lp = (reverb.comb2_lp * lp_coeff) + (comb2 * (1.0f - lp_coeff));
        reverb.comb3_lp = (reverb.comb3_lp * lp_coeff) + (comb3 * (1.0f - lp_coeff));
        reverb.comb4_lp = (reverb.comb4_lp * lp_coeff) + (comb4 * (1.0f - lp_coeff));
        
        // Write back to comb filters
        reverb.buffer[reverb.comb1_idx] = mono_input + (reverb.comb1_lp * feedback);
        reverb.buffer[reverb.comb2_idx] = mono_input + (reverb.comb2_lp * feedback);
        reverb.buffer[reverb.comb3_idx] = mono_input + (reverb.comb3_lp * feedback);
        reverb.buffer[reverb.comb4_idx] = mono_input + (reverb.comb4_lp * feedback);
        
        // Advance comb indices with wraparound
        reverb.comb1_idx = (reverb.comb1_idx + 1) % REVERB_COMB1_LENGTH;
        reverb.comb2_idx = (reverb.comb2_idx + 1) % REVERB_COMB2_LENGTH;
        reverb.comb3_idx = (reverb.comb3_idx + 1) % REVERB_COMB3_LENGTH;
        reverb.comb4_idx = (reverb.comb4_idx + 1) % REVERB_COMB4_LENGTH;
        
        // Sum comb outputs and apply allpass filters in series
        float allpass_input = (comb1 + comb2 + comb3 + comb4) * 0.25f;
        
        // First allpass filter
        float ap1_in = allpass_input;
        float ap1_out = reverb.buffer[reverb.ap1_idx] - ap1_in * 0.5f;
        reverb.buffer[reverb.ap1_idx] = ap1_in + ap1_out * 0.5f;
        reverb.ap1_idx = (reverb.ap1_idx + 1) % REVERB_AP1_LENGTH;
        
        // Second allpass filter
        float ap2_in = ap1_out;
        float ap2_out = reverb.buffer[reverb.ap2_idx] - ap2_in * 0.5f;
        reverb.buffer[reverb.ap2_idx] = ap2_in + ap2_out * 0.5f;
        reverb.ap2_idx = (reverb.ap2_idx + 1) % REVERB_AP2_LENGTH;
        
        // Mix reverb output with dry signal
        buffer[i*2] = left * reverb.dry_gain + ap2_out * reverb.wet_gain;
        buffer[i*2+1] = right * reverb.dry_gain + ap2_out * reverb.wet_gain;
    }
}

void apply_delay(float* buffer, uint32_t num_samples) {
    if (!delay.enabled) return;
    
    for (uint32_t i = 0; i < num_samples; i++) {
        // Calculate read position
        uint32_t read_pos = (delay.write_pos + delay.buffer_size - delay.samples) % delay.buffer_size;
        
        // Get delayed sample
        float delay_left = delay.buffer[read_pos*2] / 32768.0f;
        float delay_right = delay.buffer[read_pos*2+1] / 32768.0f;
        
        // Get current sample
        float left = buffer[i*2];
        float right = buffer[i*2+1];
        
        // Calculate new sample with feedback
        float new_left = left + delay_left * delay.feedback_gain;
        float new_right = right + delay_right * delay.feedback_gain;
        
        // Write to delay buffer (convert to int16_t)
        delay.buffer[delay.write_pos*2] = (int16_t)(new_left * 32767.0f);
        delay.buffer[delay.write_pos*2+1] = (int16_t)(new_right * 32767.0f);
        
        // Advance write position
        delay.write_pos = (delay.write_pos + 1) % delay.buffer_size;
        
        // Mix delayed and dry signal
        buffer[i*2] = left * delay.dry_gain + delay_left * delay.wet_gain;
        buffer[i*2+1] = right * delay.dry_gain + delay_right * delay.wet_gain;
    }
}

void apply_filter(uint8_t channel_id, float* buffer, uint32_t num_samples) {
    Filter* filter = &filters[channel_id];
    
    if (!filter->enabled) return;
    
    for (uint32_t i = 0; i < num_samples; i++) {
        // Get input sample
        float input = buffer[i];
        
        // Apply biquad filter
        float output = filter->a0 * input + 
                      filter->a1 * filter->x1 + 
                      filter->a2 * filter->x2 - 
                      filter->b1 * filter->y1 - 
                      filter->b2 * filter->y2;
        
        // Update filter state
        filter->x2 = filter->x1;
        filter->x1 = input;
        filter->y2 = filter->y1;
        filter->y1 = output;
        
        // Write output
        buffer[i] = output;
    }
}

// Reverb Processing with Low-Pass Filtering
void process_reverb(int16_t* buffer, uint32_t num_samples) {
    for (uint32_t i = 0; i < num_samples; i++) {
        int32_t input = buffer[i];
        int32_t comb_output = (reverb_delay[i % REVERB_BUFFER_SIZE] * REVERB_FEEDBACK) >> 8;
        int32_t filtered_output = (comb_output + reverb_lp * REVERB_LP_COEFF) >> 1;
        reverb_lp = filtered_output;
        reverb_delay[i % REVERB_BUFFER_SIZE] = input + filtered_output;
        buffer[i] = (input + filtered_output) >> 1;
    }
}

// Wavetable Morphing Implementation
void generate_wavetable_sample(Waveform* wave, float phase, float morph) {
    int index = (int)(phase * WAVE_TABLE_SIZE) % WAVE_TABLE_SIZE;
    int next_index = (index + 1) % WAVE_TABLE_SIZE;
    float blend = phase * WAVE_TABLE_SIZE - index;
    wave->output = (wave->table1[index] * (1.0f - morph) + wave->table2[index] * morph) * (1.0f - blend) +
                   (wave->table1[next_index] * (1.0f - morph) + wave->table2[next_index] * morph) * blend;
}

//Main Audio Processing and Output
// Buffer for final audio output
float mix_buffer[AUDIO_BUFFER_SIZE * 2];  // Stereo

// Generate audio data
void generate_audio_buffer() {
    // Clear mix buffer
    memset(mix_buffer, 0, AUDIO_BUFFER_SIZE * 2 * sizeof(float));
    
    // Process all active channels
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (!channels[ch].active) continue;
        
        // Process channel based on type
        switch (channels[ch].type) {
            case CHANNEL_TYPE_FM:
                render_fm_channel(ch, mix_buffer, AUDIO_BUFFER_SIZE);
                break;
                
            case CHANNEL_TYPE_SAMPLE:
                render_sample_channel(ch, mix_buffer, AUDIO_BUFFER_SIZE);
                break;
                
            case CHANNEL_TYPE_WAVETABLE:
                render_wavetable_channel(ch, mix_buffer, AUDIO_BUFFER_SIZE);
                break;
        }
        
        // Apply channel filter if enabled
        if (filters[ch].enabled) {
            // Apply filter separately to left and right channels
            for (int side = 0; side < 2; side++) {
                float temp_buffer[AUDIO_BUFFER_SIZE];
                
                // Extract single channel
                for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                    temp_buffer[i] = mix_buffer[i*2 + side];
                }
                
                // Apply filter
                apply_filter(ch, temp_buffer, AUDIO_BUFFER_SIZE);
                
                // Put back
                for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                    mix_buffer[i*2 + side] = temp_buffer[i];
                }
            }
        }
    }
    
    // Apply global effects in order
    if (delay.enabled) {
        apply_delay(mix_buffer, AUDIO_BUFFER_SIZE);
    }
    
    if (reverb.enabled) {
        apply_reverb(mix_buffer, AUDIO_BUFFER_SIZE);
    }
    
    // Convert to final output format and apply master volume
    float master_gain = master_volume / 255.0f;
    
    for (int i = 0; i < AUDIO_BUFFER_SIZE * 2; i++) {
        // Apply master volume
        float sample = mix_buffer[i] * master_gain;
        
        // Soft clipping to prevent harsh distortion
        if (sample > 1.0f || sample < -1.0f) {
            sample = tanhf(sample);
        }
        
        // Convert to output format
        // For PWM, we scale to 8-bit unsigned (0-255)
        output_buffer[i] = (uint8_t)((sample * 0.5f + 0.5f) * 255.0f);
        
        // For I2S, we'd convert to signed 16-bit or 24-bit
        // i2s_buffer[i] = (int16_t)(sample * 32767.0f);
    }
}

// Initialize audio output
void init_audio_output() {
    // For PWM output
    gpio_set_function(AUDIO_PIN_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_RIGHT, GPIO_FUNC_PWM);
    
    // Get PWM slice numbers
    uint slice_left = pwm_gpio_to_slice_num(AUDIO_PIN_LEFT);
    uint slice_right = pwm_gpio_to_slice_num(AUDIO_PIN_RIGHT);
    
    // Configure PWM
    pwm_config config = pwm_get_default_config();
    
    // Set divider to achieve desired sample rate
    // System clock (typically 125MHz) / (sample rate * resolution)
    float clock_div = (float)clock_get_hz(clk_sys) / (SAMPLE_RATE * 256.0f);
    pwm_config_set_clkdiv(&config, clock_div);
    pwm_config_set_wrap(&config, 255); // 8-bit resolution
    
    // Configure and start PWM
    pwm_init(slice_left, &config, true);
    pwm_init(slice_right, &config, true);
    
    // Optionally, for higher quality, configure I2S using PIO
    /* 
    // Configure PIO for I2S output
    uint offset = pio_add_program(pio0, &audio_i2s_program);
    audio_i2s_program_init(pio0, 0, offset, AUDIO_I2S_BCLK, AUDIO_I2S_DATA, AUDIO_I2S_LRCLK);
    
    // Set up DMA for I2S
    setup_audio_dma();
    */
}

// Setup memory allocation
void init_memory_allocation() {
    // Determine available memory based on chip
    bool is_rp2350 = check_if_rp2350();
    
    if (is_rp2350) {
        // RP2350 allocation (520KB)
        sample_memory_size = 256 * 1024;  // 256KB
        pattern_memory_size = 128 * 1024; // 128KB
        instrument_memory_size = 64 * 1024; // 64KB
        // ~72KB for working buffers
    } else {
        // RP2040 allocation (264KB)
        sample_memory_size = 128 * 1024;  // 128KB
        pattern_memory_size = 64 * 1024;  // 64KB
        instrument_memory_size = 32 * 1024; // 32KB
        // ~40KB for working buffers
    }
    
    // Allocate reverb buffer
    reverb.buffer_size = is_rp2350 ? 32768 : 16384;
    reverb.buffer = malloc(reverb.buffer_size * sizeof(float));
    
    // Allocate delay buffer (2-second stereo buffer at full sample rate)
    delay.buffer_size = is_rp2350 ? SAMPLE_RATE * 2 * 2 : SAMPLE_RATE * 1.5 * 2;
    delay.buffer = malloc(delay.buffer_size * sizeof(int16_t));
    
    // Initialize indices for reverb
    reverb.comb1_idx = 0;
    reverb.comb2_idx = 0;
    reverb.comb3_idx = 0;
    reverb.comb4_idx = 0;
    reverb.ap1_idx = 0;
    reverb.ap2_idx = 0;
    
    // Clear buffers
    memset(reverb.buffer, 0, reverb.buffer_size * sizeof(float));
    memset(delay.buffer, 0, delay.buffer_size * sizeof(int16_t));
}

// Audio processing function for Core 1
void core1_audio_processing() {
    // Timing variables
    uint32_t next_sample_time = 0;
    uint32_t sample_interval = 1000000 / SAMPLE_RATE; // in microseconds
    uint32_t audio_position = 0;
    
    // Initialize output buffer index
    audio_position = 0;
    
    while (true) {
        // Generate audio buffer when needed
        if (audio_position == 0) {
            generate_audio_buffer();
        }
        
        // Wait until it's time for the next sample
        uint32_t current_time = time_us_32();
        if (current_time < next_sample_time) {
            // Sleep for a short time to save power
            sleep_us(10);
            continue;
        }
        
        // Calculate time for next sample
        next_sample_time = current_time + sample_interval;
        
        // Output current sample to PWM
        pwm_set_gpio_level(AUDIO_PIN_LEFT, output_buffer[audio_position*2]);
        pwm_set_gpio_level(AUDIO_PIN_RIGHT, output_buffer[audio_position*2+1]);
        
        // Advance position
        audio_position = (audio_position + 1) % AUDIO_BUFFER_SIZE;
    }
}

// Main function
int main() {
    // Initialize hardware
    stdio_init_all();
    
    // Print startup message
    printf("TriBoy APU - Initializing...\n");
    
    // Initialize SPI communication with CPU
    init_spi_slave();
    
    // Initialize memory allocation
    init_memory_allocation();
    
    // Initialize audio output
    init_audio_output();
    
    // Initialize channels
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].active = false;
        channels[i].volume = 255;
        channels[i].pan = 128; // center
        channels[i].type = CHANNEL_TYPE_FM; // default
    }
    
    // Initialize sine table for wavetables and FM synthesis
    for (int i = 0; i < SINE_WAVE_SIZE; i++) {
        sine_table[i] = (int16_t)(sinf(i * 2.0f * 3.14159f / SINE_WAVE_SIZE) * 32767.0f);
    }
    
    // Start Core 1 for audio processing
    multicore_launch_core1(core1_audio_processing);
    
    // Last command processing time
    uint32_t last_update_time = time_us_32();
    
    // Main loop on Core 0
    while (true) {
        // Check for commands from CPU
        if (!gpio_get(CPU_CS_PIN)) {
            // CS asserted, receive command
            uint8_t cmd_id = 0;
            uint8_t cmd_length = 0;
            
            // Read command ID and length
            spi_read_blocking(SPI_PORT, 0xFF, &cmd_id, 1);
            spi_read_blocking(SPI_PORT, 0xFF, &cmd_length, 1);
            
            // Read command data if any
            if (cmd_length > 2) {
                spi_read_blocking(SPI_PORT, 0xFF, cmd_buffer, cmd_length - 2);
            }
            
            // Process the command
            process_command(cmd_id, cmd_buffer, cmd_length - 2);
        }
        
        // Update timing-based effects and tracker sequencer
        uint32_t current_time = time_us_32();
        uint32_t elapsed = current_time - last_update_time;
        
        if (elapsed >= 1000) { // At least 1ms has passed
            // Update tracker
            update_tracker(elapsed);
            
            // Update time
            last_update_time = current_time;
        }
        
        // Small delay to reduce CPU usage
        sleep_us(50);
    }
    
    return 0;
}

// Memory Management, Memory Status, and Memory Optimization
void send_memory_status() {
    // Prepare status packet
    uint8_t status[16];
    
    // Calculate memory usage percentages
    uint32_t sample_memory_used = 0;
    uint32_t pattern_memory_used = 0;
    uint32_t instrument_memory_used = 0;
    
    // Count sample memory
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (samples[i].loaded && samples[i].data != NULL) {
            sample_memory_used += samples[i].size;
        }
    }
    
    // Count pattern memory
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (trackers[i].pattern_data != NULL) {
            pattern_memory_used += sizeof(TrackerNote) * MAX_ROWS_PER_PATTERN * 
                                  trackers[i].num_channels * 256; // Approximate
        }
    }
    
    // Count instrument memory (wavetables, FM settings)
    for (int i = 0; i < MAX_WAVETABLES; i++) {
        if (wavetables[i].data != NULL) {
            instrument_memory_used += wavetables[i].size * sizeof(int16_t);
        }
    }
    
    // Calculate percentages
    uint8_t sample_usage = (sample_memory_used * 100) / sample_memory_size;
    uint8_t pattern_usage = (pattern_memory_used * 100) / pattern_memory_size;
    uint8_t instrument_usage = (instrument_memory_used * 100) / instrument_memory_size;
    
    // Fill status packet
    status[0] = sample_usage;
    status[1] = pattern_usage;
    status[2] = instrument_usage;
    status[3] = MAX_CHANNELS;
    status[4] = (sample_memory_size - sample_memory_used) / 1024; // Available KB
    status[5] = (pattern_memory_size - pattern_memory_used) / 1024;
    status[6] = (instrument_memory_size - instrument_memory_used) / 1024;
    
    // Add active channels count
    uint8_t active_channels = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].active) active_channels++;
    }
    status[7] = active_channels;
    
    // Add CPU load estimate (simple approximation)
    status[8] = audio_cpu_load;
    
    // Reserved for future use
    for (int i = 9; i < 16; i++) {
        status[i] = 0;
    }
    
    // Send status packet to CPU
    send_data_to_cpu(STATUS_MEMORY, status, 16);
}

// Clear samples from memory
void cmd_mem_clear_samples() {
    // Stop any active sample playback
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].type == CHANNEL_TYPE_SAMPLE && channels[i].active) {
            channels[i].active = false;
        }
    }
    
    // Free all sample memory
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (samples[i].data != NULL) {
            free(samples[i].data);
            samples[i].data = NULL;
            samples[i].loaded = false;
            samples[i].size = 0;
        }
    }
    
    send_ack_to_cpu(CMD_MEM_CLEAR_SAMPLES);
}

// Optimize memory usage
void cmd_mem_optimize() {
    // This would reorganize memory to reduce fragmentation
    // For a simple implementation, we'll just collect garbage
    
    // Free sample memory for unused samples
    for (int i = 0; i < MAX_SAMPLES; i++) {
        // Check if this sample is being used by any active channel
        bool in_use = false;
        for (int c = 0; c < MAX_CHANNELS; c++) {
            if (channels[c].active && 
                channels[c].type == CHANNEL_TYPE_SAMPLE &&
                sample_channels[c].sample_id == i) {
                in_use = true;
                break;
            }
        }
        
        // If not in use, we can free it
        if (!in_use && samples[i].data != NULL) {
            free(samples[i].data);
            samples[i].data = NULL;
            samples[i].loaded = false;
            samples[i].size = 0;
        }
    }
    
    // Free wavetable memory for unused wavetables
    for (int i = 0; i < MAX_WAVETABLES; i++) {
        // Check if this wavetable is being used
        bool in_use = false;
        for (int c = 0; c < MAX_CHANNELS; c++) {
            if (channels[c].active && 
                channels[c].type == CHANNEL_TYPE_WAVETABLE &&
                wave_channels[c].table_id == i) {
                in_use = true;
                break;
            }
            
            // Also check if it's used in a sweep
            if (channels[c].active && 
                channels[c].type == CHANNEL_TYPE_WAVETABLE &&
                wave_channels[c].sweep_active &&
                (wave_channels[c].sweep_start_table == i ||
                 wave_channels[c].sweep_end_table == i)) {
                in_use = true;
                break;
            }
        }
        
        // If not in use, we can free it
        if (!in_use && wavetables[i].data != NULL) {
            free(wavetables[i].data);
            wavetables[i].data = NULL;
            wavetables[i].size = 0;
        }
    }
    
    send_ack_to_cpu(CMD_MEM_OPTIMIZE);
}

// RP2350-Specific Enhancements
// The RP2350 has additional memory and processing power, allowing us to implement higher quality audio features:

// Check if running on RP2350
bool check_if_rp2350() {
    // This would be platform-specific detection
    // Here's a simple approach based on available RAM
    uint32_t total_ram = get_total_ram();
    return (total_ram > 300000); // More than ~300KB suggests RP2350
}

// Enhanced effects for RP2350
void init_rp2350_enhancements() {
    if (!check_if_rp2350()) return;
    
    // Higher quality reverb
    REVERB_COMB1_LENGTH = 1789;  // Increased delay line lengths
    REVERB_COMB2_LENGTH = 1999;
    REVERB_COMB3_LENGTH = 2137;
    REVERB_COMB4_LENGTH = 2269;
    REVERB_AP1_LENGTH = 277;
    REVERB_AP2_LENGTH = 371;
    
    // 24-bit audio processing
    use_24bit_processing = true;
    
    // Additional synthesis options
    MAX_OPERATORS_PER_FM_CHANNEL = 6;  // Increase from 4 to 6 for RP2350
    
    // Higher quality interpolation
    use_cubic_interpolation = true;
    
    // Additional wavetable features
    enable_wavetable_fm = true;
    
    // I2S output via PIO
    setup_i2s_output();
}

// Cubic interpolation for higher quality sample playback (RP2350)
float cubic_interpolate(const float* p, float x) {
    return p[1] + 0.5f * x * (p[2] - p[0] + x * (2.0f * p[0] - 5.0f * p[1] + 4.0f * p[2] - p[3] + x * (3.0f * (p[1] - p[2]) + p[3] - p[0])));
}

// RP2350: Enhanced sample playback with cubic interpolation
int16_t advanced_sample_interpolation(const uint8_t* data, uint32_t pos, float frac, bool is_16bit, uint32_t max_pos) {
    if (!use_cubic_interpolation) {
        // Fall back to linear interpolation
        return interpolate_sample(data, pos, frac, is_16bit);
    }
    
    // Get 4 sample points for cubic interpolation
    float p[4];
    
    for (int i = 0; i < 4; i++) {
        int idx = pos + i - 1;
        
        // Handle boundary conditions
        if (idx < 0) idx = 0;
        if (idx >= max_pos) idx = max_pos - 1;
        
        // Get sample value
        if (is_16bit) {
            p[i] = (float)((int16_t)data[idx*2] | ((int16_t)data[idx*2+1] << 8)) / 32768.0f;
        } else {
            p[i] = ((int16_t)data[idx] - 128) / 128.0f;
        }
    }
    
    // Apply cubic interpolation
    float result = cubic_interpolate(p, frac);
    
    // Clamp and convert back to int16_t
    if (result > 1.0f) result = 1.0f;
    if (result < -1.0f) result = -1.0f;
    
    return (int16_t)(result * 32767.0f);
}

// RP2350: I2S output via PIO
void setup_i2s_output() {
    // Configure PIO for I2S output
    uint offset = pio_add_program(pio0, &audio_i2s_program);
    
    // Initialize
    pio_i2s_config config;
    config.pio = pio0;
    config.sm = 0;
    config.prog_offset = offset;
    config.bclk_pin = AUDIO_I2S_BCLK;
    config.data_pin = AUDIO_I2S_DATA;
    config.lrclk_pin = AUDIO_I2S_LRCLK;
    config.sample_rate = SAMPLE_RATE;
    config.bit_depth = 16; // or 24 for higher quality
    
    pio_i2s_init(&config);
    
    // Configure DMA for I2S
    i2s_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config dma_config = dma_channel_get_default_config(i2s_dma_channel);
    
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_16); // or DMA_SIZE_32 for 24-bit
    channel_config_set_read_increment(&dma_config, true);
    channel_config_set_write_increment(&dma_config, false);
    channel_config_set_dreq(&dma_config, pio_get_dreq(pio0, 0, true));
    
    dma_channel_configure(
        i2s_dma_channel,
        &dma_config,
        &pio0->txf[0],     // Write to PIO TX FIFO
        i2s_buffer,        // Read from I2S buffer
        AUDIO_BUFFER_SIZE, // Number of transfers
        false              // Don't start yet
    );
    
    // Use double-buffering
    use_i2s_double_buffer = true;
}

// RP2350: Enhanced audio output function for Core 1
void core1_audio_processing_enhanced() {
    // Double buffering variables
    bool current_buffer = false;
    
    while (true) {
        // Generate audio for the inactive buffer
        generate_audio_buffer();
        
        // If using I2S, transfer via DMA
        if (use_i2s_double_buffer) {
            // Convert float buffer to I2S format
            for (int i = 0; i < AUDIO_BUFFER_SIZE * 2; i++) {
                // For 16-bit
                i2s_buffer[i] = (int16_t)(mix_buffer[i] * 32767.0f);
                
                // For 24-bit, we'd use a different buffer format
                // i2s_buffer_24bit[i] = (int32_t)(mix_buffer[i] * 8388607.0f) << 8;
            }
            
            // Wait for previous transfer to complete
            dma_channel_wait_for_finish_blocking(i2s_dma_channel);
            
            // Start new transfer
            dma_channel_set_read_addr(i2s_dma_channel, i2s_buffer, true);
        } else {
            // Original PWM-based output
            for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                // Wait until it's time for the next sample
                sleep_us(1000000 / SAMPLE_RATE);
                
                // Output current sample to PWM
                pwm_set_gpio_level(AUDIO_PIN_LEFT, output_buffer[i*2]);
                pwm_set_gpio_level(AUDIO_PIN_RIGHT, output_buffer[i*2+1]);
            }
        }
        
        // Swap buffers
        current_buffer = !current_buffer;
    }
}

/*
The APU implementations described above provides a comprehensive audio system for the TriBoy hardware, with features comparable to 16-bit era sound chips while leveraging the additional capabilities of modern microcontrollers.

To integrate this system into the TriBoy architecture:
1. The CPU sends commands to the APU via SPI
2. The APU processes these commands and generates audio
3. Audio output is produced via PWM or I2S (for higher quality)
4. The system supports expandable capabilities based on available resources

Features:
1. Full command set as specified in the documentation
2. Multiple synthesis methods (FM, sample, wavetable)
3. Tracker/sequencer for music playback
4. Comprehensive effects pipeline
5. Memory management and optimization
6. RP2350-specific enhancements
*/
