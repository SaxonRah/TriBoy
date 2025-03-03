// apu_main.c - TriBoy APU implementation
// Simple yet expandable setup for implementing the TriBoy three-microcontroller architecture.
// Never compiled, just an example.

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

// Pin definitions
#define CPU_SPI_PORT spi0
#define CPU_MISO_PIN 0
#define CPU_MOSI_PIN 1
#define CPU_SCK_PIN 2
#define CPU_CS_PIN 3
#define AUDIO_PIN_LEFT 20
#define AUDIO_PIN_RIGHT 21

// Audio configuration
#define SAMPLE_RATE 44100
#define AUDIO_BUFFER_SIZE 1024
#define MAX_CHANNELS 16
#define MAX_SAMPLES 64
#define SINE_WAVE_SIZE 256

// Command buffer
#define CMD_BUFFER_SIZE 256
uint8_t cmd_buffer[CMD_BUFFER_SIZE];

// Audio buffers
int16_t audio_buffer[AUDIO_BUFFER_SIZE * 2]; // Stereo
volatile uint32_t audio_position = 0;
uint8_t master_volume = 200; // 0-255

// Sine wave table for testing and synthesis
int16_t sine_table[SINE_WAVE_SIZE];

// Channel structure
typedef struct {
    bool active;
    uint8_t type;       // 0=none, 1=tone, 2=sample, 3=noise
    uint8_t volume;     // 0-255
    uint8_t pan;        // 0=left, 128=center, 255=right
    float frequency;    // Hz
    uint32_t phase;     // Current phase (accumulator)
    uint32_t phase_inc; // Phase increment per sample
    uint8_t sample_id;  // For sample playback
    uint32_t sample_pos; // Position in sample
    float sample_step;  // Sample position increment
    bool loop;          // Loop sample
} Channel;

// Sample structure
typedef struct {
    bool loaded;
    uint8_t* data;
    uint32_t size;
    uint32_t loop_start;
    uint32_t loop_end;
    uint8_t format;     // 0=8-bit, 1=16-bit
} Sample;

// Tracker/sequencer state
typedef struct {
    bool playing;
    uint8_t tempo;
    uint8_t current_pattern;
    uint8_t current_row;
    uint8_t tick_counter;
    uint8_t ticks_per_row;
} Tracker;

// Global state
Channel channels[MAX_CHANNELS];
Sample samples[MAX_SAMPLES];
Tracker tracker;
uint8_t pwm_slice_left;
uint8_t pwm_slice_right;

// Initialize hardware
void init_hardware() {
    stdio_init_all();
    printf("TriBoy APU initializing...\n");
    
    // Set up SPI slave for communication with CPU
    spi_init(CPU_SPI_PORT, 20000000);
    spi_set_slave(CPU_SPI_PORT, true);
    
    gpio_set_function(CPU_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CPU_MISO_PIN, GPIO_FUNC_SPI);
    
    gpio_init(CPU_CS_PIN);
    gpio_set_dir(CPU_CS_PIN, GPIO_IN);
    gpio_pull_up(CPU_CS_PIN);
    
    // Set up audio PWM
    gpio_set_function(AUDIO_PIN_LEFT, GPIO_FUNC_PWM);
    gpio_set_function(AUDIO_PIN_RIGHT, GPIO_FUNC_PWM);
    
    pwm_slice_left = pwm_gpio_to_slice_num(AUDIO_PIN_LEFT);
    pwm_slice_right = pwm_gpio_to_slice_num(AUDIO_PIN_RIGHT);
    
    // Set up PWM for audio
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (SAMPLE_RATE * 256.0f));
    pwm_config_set_wrap(&config, 255);
    
    pwm_init(pwm_slice_left, &config, true);
    pwm_init(pwm_slice_right, &config, true);
    
    // Initialize sine table for synthesis
    for (int i = 0; i < SINE_WAVE_SIZE; i++) {
        sine_table[i] = (int16_t)(sinf(i * 2.0f * 3.14159f / SINE_WAVE_SIZE) * 32767.0f);
    }
    
    // Initialize audio buffer
    memset(audio_buffer, 0, sizeof(audio_buffer));
    
    // Initialize channels
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].active = false;
        channels[i].volume = 255;
        channels[i].pan = 128;
    }
    
    // Initialize tracker
    tracker.playing = false;
    tracker.tempo = 120;
    tracker.current_pattern = 0;
    tracker.current_row = 0;
    tracker.tick_counter = 0;
    tracker.ticks_per_row = 6; // 6 ticks per row at 120 BPM gives classic tracker timing
}

// Audio generation functions
void generate_audio_buffer() {
    // Clear the audio buffer
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE * 2 * sizeof(int16_t));
    
    // Mix all active channels
    for (int c = 0; c < MAX_CHANNELS; c++) {
        Channel* channel = &channels[c];
        
        if (!channel->active) continue;
        
        // Calculate effective volume for left/right channels
        float vol_left = (channel->volume * (255 - channel->pan) * master_volume) / (255.0f * 255.0f * 255.0f);
        float vol_right = (channel->volume * channel->pan * master_volume) / (255.0f * 255.0f * 255.0f);
        
        if (channel->type == 1) { // Tone generator
            // Generate simple tone
            for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                // Get sine wave value from table
                int16_t sample = sine_table[channel->phase >> 24];
                
                // Mix into stereo buffer
                audio_buffer[i*2] += (int16_t)(sample * vol_left);
                audio_buffer[i*2+1] += (int16_t)(sample * vol_right);
                
                // Advance phase
                channel->phase += channel->phase_inc;
            }
        }
        else if (channel->type == 2 && channel->sample_id < MAX_SAMPLES) { // Sample playback
            Sample* sample = &samples[channel->sample_id];
            
            if (!sample->loaded || !sample->data) continue;
            
            for (int i = 0; i < AUDIO_BUFFER_SIZE; i++) {
                // Check if we've reached the end of the sample
                if (channel->sample_pos >= sample->size) {
                    if (channel->loop && sample->loop_end > sample->loop_start) {
                        channel->sample_pos = sample->loop_start;
                    } else {
                        channel->active = false;
                        break;
                    }
                }
                
                // Get sample value
                int16_t sample_value;
                if (sample->format == 0) { // 8-bit
                    sample_value = ((int16_t)sample->data[channel->sample_pos] - 128) * 256;
                } else { // 16-bit
                    sample_value = ((int16_t)sample->data[channel->sample_pos] | 
                                  ((int16_t)sample->data[channel->sample_pos + 1] << 8));
                    channel->sample_pos++; // Extra increment for 16-bit
                }
                
                // Mix into stereo buffer
                audio_buffer[i*2] += (int16_t)(sample_value * vol_left);
                audio_buffer[i*2+1] += (int16_t)(sample_value * vol_right);
                
                // Advance sample position
                channel->sample_pos++;
                
                // Implement proper sample rate conversion in a real implementation
            }
        }
    }
}

// PWM callback for audio
void pwm_audio_callback() {
    // Set PWM levels based on current sample
    pwm_set_gpio_level(AUDIO_PIN_LEFT, (audio_buffer[audio_position*2] >> 8) + 128);
    pwm_set_gpio_level(AUDIO_PIN_RIGHT, (audio_buffer[audio_position*2+1] >> 8) + 128);
    
    // Advance position
    audio_position = (audio_position + 1) % AUDIO_BUFFER_SIZE;
    
    // If we're at the end of the buffer, generate more audio
    if (audio_position == 0) {
        generate_audio_buffer();
    }
}

// Command processing functions
void process_command(uint8_t cmd_id, const uint8_t* data, uint8_t length) {
    switch (cmd_id) {
        case 0x00: // NOP
            break;
            
        case 0x01: // RESET_AUDIO
            for (int i = 0; i < MAX_CHANNELS; i++) {
                channels[i].active = false;
            }
            tracker.playing = false;
            audio_position = 0;
            memset(audio_buffer, 0, sizeof(audio_buffer));
            break;
            
        case 0x02: // SET_MASTER_VOLUME
            master_volume = data[0];
            break;
            
        case 0x11: // TRACKER_PLAY
            // In a real implementation, this would start a music track
            // For this demo, we'll just start a simple tone sequence
            tracker.playing = true;
            tracker.current_pattern = 0;
            tracker.current_row = 0;
            tracker.tick_counter = 0;
            
            // Start a couple of channels with different tones
            channels[0].active = true;
            channels[0].type = 1; // tone
            channels[0].frequency = 440.0f; // A4
            channels[0].phase = 0;
            channels[0].phase_inc = (uint32_t)(channels[0].frequency * 4294967296.0f / SAMPLE_RATE);
            channels[0].volume = 128;
            channels[0].pan = 90; // slightly left
            
            channels[1].active = true;
            channels[1].type = 1; // tone
            channels[1].frequency = 659.25f; // E5
            channels[1].phase = 0;
            channels[1].phase_inc = (uint32_t)(channels[1].frequency * 4294967296.0f / SAMPLE_RATE);
            channels[1].volume = 128;
            channels[1].pan = 160; // slightly right
            break;
            
        case 0x30: // CHANNEL_SET_VOLUME
            {
                uint8_t channel_id = data[0];
                if (channel_id < MAX_CHANNELS) {
                    channels[channel_id].volume = data[1];
                }
            }
            break;
            
        case 0x31: // CHANNEL_SET_PAN
            {
                uint8_t channel_id = data[0];
                if (channel_id < MAX_CHANNELS) {
                    channels[channel_id].pan = data[1];
                }
            }
            break;
            
        case 0x32: // CHANNEL_SET_PITCH
            {
                uint8_t channel_id = data[0];
                if (channel_id < MAX_CHANNELS) {
                    uint16_t pitch_value = (data[1] << 8) | data[2];
                    
                    // Convert pitch value to frequency (simple mapping)
                    float frequency = 440.0f * powf(2.0f, (pitch_value - 69 * 16) / (12.0f * 16.0f));
                    
                    channels[channel_id].frequency = frequency;
                    channels[channel_id].phase_inc = (uint32_t)(frequency * 4294967296.0f / SAMPLE_RATE);
                }
            }
            break;
            
        case 0x33: // CHANNEL_NOTE_ON
            {
                uint8_t channel_id = data[0];
                uint8_t note = data[1];
                uint8_t velocity = data[2];
                
                if (channel_id < MAX_CHANNELS) {
                    // MIDI note to frequency conversion
                    float frequency = 440.0f * powf(2.0f, (note - 69) / 12.0f);
                    
                    channels[channel_id].active = true;
                    channels[channel_id].type = 1; // tone
                    channels[channel_id].frequency = frequency;
                    channels[channel_id].phase = 0;
                    channels[channel_id].phase_inc = (uint32_t)(frequency * 4294967296.0f / SAMPLE_RATE);
                    channels[channel_id].volume = velocity * 2; // Scale 0-127 to 0-254
                }
            }
            break;
            
        case 0x34: // CHANNEL_NOTE_OFF
            {
                uint8_t channel_id = data[0];
                if (channel_id < MAX_CHANNELS) {
                    channels[channel_id].active = false;
                }
            }
            break;
            
        case 0x70: // SAMPLE_LOAD
            {
                uint8_t sample_id = data[0];
                uint8_t format = data[1];
                uint16_t sample_rate = (data[2] | (data[3] << 8));
                uint16_t loop_start = (data[4] | (data[5] << 8));
                uint16_t loop_end = (data[6] | (data[7] << 8));
                uint16_t size = (data[8] | (data[9] << 8));
                
                // In a real implementation, this would load the sample
                // For this demo, we'll just acknowledge the command
                printf("Sample %d load requested (format: %d, size: %d)\n", 
                       sample_id, format, size);
                
                // Mark sample as loaded but data will be loaded by subsequent command
                if (sample_id < MAX_SAMPLES) {
                    samples[sample_id].loaded = true;
                    samples[sample_id].format = format;
                    samples[sample_id].size = size;
                    samples[sample_id].loop_start = loop_start;
                    samples[sample_id].loop_end = loop_end;
                    
                    // Allocate memory for the sample
                    if (samples[sample_id].data) {
                        free(samples[sample_id].data);
                    }
                    samples[sample_id].data = malloc(size);
                }
            }
            break;
            
        case 0x71: // SAMPLE_PLAY
            {
                uint8_t channel_id = data[0];
                uint8_t sample_id = data[1];
                uint8_t pitch = data[2];
                uint8_t volume = data[3];
                
                if (channel_id < MAX_CHANNELS && sample_id < MAX_SAMPLES && samples[sample_id].loaded) {
                    channels[channel_id].active = true;
                    channels[channel_id].type = 2; // sample
                    channels[channel_id].sample_id = sample_id;
                    channels[channel_id].sample_pos = 0;
                    channels[channel_id].volume = volume;
                    
                    // Calculate pitch shift
                    // 128 = original pitch, 64 = octave down, 192 = octave up
                    float pitch_shift = powf(2.0f, (pitch - 128) / 64.0f);
                    channels[channel_id].sample_step = pitch_shift;
                }
            }
            break;
            
        case 0xF0: // Custom data transfer command for sample data
            {
                // This would receive sample data chunks in a real implementation
                // For this demo, we'll just acknowledge it
                printf("Sample data chunk received (%d bytes)\n", length);
            }
            break;
            
        // Add more commands as needed
    }
}

// Core 1 audio processing function
void core1_entry() {
    printf("APU Core 1 started\n");
    
    // Generate initial audio buffer
    generate_audio_buffer();
    
    // Main audio processing loop
    while (true) {
        // Call PWM audio callback at sample rate frequency
        pwm_audio_callback();
        
        // Wait for next sample
        sleep_us(1000000 / SAMPLE_RATE);
    }
}

// Main function
int main() {
    // Initialize hardware
    init_hardware();
    
    // Start Core 1 for audio processing
    multicore_launch_core1(core1_entry);
    
    // Create a simple test tone
    channels[0].active = true;
    channels[0].type = 1; // tone
    channels[0].frequency = 440.0f; // A4
    channels[0].phase = 0;
    channels[0].phase_inc = (uint32_t)(channels[0].frequency * 4294967296.0f / SAMPLE_RATE);
    channels[0].volume = 200;
    channels[0].pan = 128; // center
    
    // Generate initial audio
    generate_audio_buffer();
    
    // Set up timer for tracker updates
    uint32_t last_tick_time = time_us_32();
    uint32_t tick_interval = 1000000 / (tracker.tempo * tracker.ticks_per_row / 60);
    
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
        
        // Update tracker if active
        if (tracker.playing) {
            uint32_t current_time = time_us_32();
            if (current_time - last_tick_time >= tick_interval) {
                last_tick_time = current_time;
                
                // Process tracker tick
                tracker.tick_counter++;
                if (tracker.tick_counter >= tracker.ticks_per_row) {
                    tracker.tick_counter = 0;
                    tracker.current_row++;
                    
                    // Simple 16-row pattern demo
                    if (tracker.current_row >= 16) {
                        tracker.current_row = 0;
                        
                        // Change notes for demo
                        if (channels[0].active) {
                            // Alternate between A4 and C5
                            if (channels[0].frequency == 440.0f) {
                                channels[0].frequency = 523.25f; // C5
                            } else {
                                channels[0].frequency = 440.0f; // A4
                            }
                            channels[0].phase_inc = (uint32_t)(channels[0].frequency * 4294967296.0f / SAMPLE_RATE);
                        }
                        
                        if (channels[1].active) {
                            // Alternate between E5 and G5
                            if (channels[1].frequency == 659.25f) {
                                channels[1].frequency = 783.99f; // G5
                            } else {
                                channels[1].frequency = 659.25f; // E5
                            }
                            channels[1].phase_inc = (uint32_t)(channels[1].frequency * 4294967296.0f / SAMPLE_RATE);
                        }
                    }
                }
            }
        }
    }
    
    return 0;
}
