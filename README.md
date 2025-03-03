# TriBoy 
## A Conceptual Three Microcontroller Architecture (TMA)
The TriBoy project represents an innovative approach to handheld game console design, leveraging multiple microcontrollers to achieve capabilities beyond what a single microcontroller could accomplish.
With its retro-inspired design and modern implementation, it provides a platform for both nostalgic 16-bit style games and modern 2D gaming experiences.

TriBoy is a conceptual handheld game console that leverages the power of multiple RP2040 (Raspberry Pi Pico) or RP2350 (Raspberry Pi Pico 2) microcontrollers in a unique Three Microcontroller Architecture (TMA).
By dedicating separate microcontrollers to CPU (Central Processing Unit), GPU (Graphics Processing Unit), and APU (Audio Processing Unit) functions, TriBoy achieves 16-bit era gaming capabilities with modern implementation techniques. 

- `README.md` goes over the concept of the TriBoy and TMA at a high-level.
- `Idea.md` goes over the ideas behind the concept for the TriBoy and TMA.
- `CPU.md` goes over the CPU design details.
- `CPU_Implementation.md` goes over the CPU design's implementation details.
- `GPU.md` goes over the GPU design details.
- `GPU_Implementation.md` goes over the GPU design's implementation details.
- `APU.md` goes over the APU design details.
- `APU_Implementation.md` goes over the APU design's implementation details.
- `ExternalCartridgeSupport.md` goes over potential external cartridge support.

## Architecture Overview
TriBoy employs a Three Microcontroller Architecture (TMA) where each microcontroller handles specific tasks:

1. **CPU Microcontroller**: Game logic, asset management, input handling, and coordination
2. **GPU Microcontroller**: Graphics rendering, sprites, background layers, and display output
3. **APU Microcontroller**: Music, sound effects, synthesis, and audio output

Each microcontroller uses both cores of the RP2040/RP2350, with optimized memory allocation and communication protocols. This distributed approach allows each processor to focus on specialized tasks, aimed to provide performance similar to 16-bit consoles like the Sega Genesis. Actual performance is yet to be discovered. 

## CPU Microcontroller
The CPU acts as the master controller, handling:

### Core Functions
- **Game Loop**: Manages game state, physics, and logic
- **Asset Management**: Loads assets from SD card
- **Input Processing**: Handles button and D-pad inputs
- **Communication**: Coordinates with GPU and APU

### Implementation Details
- **Core 0**: Dedicated to game logic and main loop
- **Core 1**: Handles system management, asset loading, and communication queues
- **Command Queues**: Prioritized queues for GPU and APU commands
- **SD Card Interface**: Loads game data and assets
- **High-level APIs**: Simple game development functions

### Boot Process
The CPU handles system initialization, SD card mounting, GPU/APU initialization, asset loading, and game execution.

```c
// Example of CPU's game loop
void run_game_loop() {
    uint32_t frame_start = time_us_32();
    
    process_input();
    update_game_state();
    check_asset_requirements();
    prepare_render_commands();
    prepare_audio_commands();
    
    // Frame timing and synchronization
    uint32_t frame_time = time_us_32() - frame_start;
    uint32_t target_frame_time = 16667; // 1/60 sec
    if (frame_time < target_frame_time) {
        sleep_us(target_frame_time - frame_time);
    }
}
```

## GPU Microcontroller
The GPU handles all graphics rendering tasks:

### Core Features
- **Background Layers**: Multiple scrollable background layers
- **Sprite System**: Hardware sprites with scaling, rotation, and animations
- **Special Effects**: Fades, scanline effects, mosaic, window clipping
- **Direct Drawing**: Pixel, line, rectangle, and circle drawing commands
- **Memory Optimization**: Dirty rectangle tracking and tile caching

### Implementation Highlights
- **Core 0**: Command processing and background rendering
- **Core 1**: Sprite handling and final compositing
- **PIO Blocks**: Custom display interfaces and hardware acceleration
- **Dual Framebuffer**: Double buffering on RP2350 models
- **Sega Genesis Inspirations**: Dual playfield mode, cell-based sprites

### Command Structure
Each GPU command follows a consistent structure:
```
1 byte: Command ID
1 byte: Command length (including ID and length bytes)
N bytes: Command parameters
```

Example GPU commands:
```c
// Set sprite position
void set_sprite(uint8_t sprite_id, int16_t x, int16_t y) {
    uint8_t data[5] = {sprite_id, (x >> 8) & 0xFF, x & 0xFF, (y >> 8) & 0xFF, y & 0xFF};
    queue_gpu_command(0x42, 6, data);
}

// Scroll background layer
void scroll_background(uint8_t layer_id, int16_t x, int16_t y) {
    uint8_t data[5] = {layer_id, (x >> 8) & 0xFF, x & 0xFF, (y >> 8) & 0xFF, y & 0xFF};
    queue_gpu_command(0x23, 6, data);
}
```

## APU Microcontroller
The APU handles all audio tasks:

### Core Features
- **Tracker System**: 16-channel music playback with patterns and instruments
- **Sample Playback**: PCM sample playback with pitch shifting and looping
- **FM Synthesis**: 4-operator FM synthesis with multiple algorithms
- **Wavetable Synthesis**: Customizable wavetables with morphing
- **Effects Processing**: Reverb, delay, filtering, and EQ

### Implementation Highlights
- **Core 0**: Audio synthesis and mixing
- **Core 1**: Command processing and sequencer
- **PIO Blocks**: I2S or PWM audio output
- **Memory Management**: Optimized allocation for samples, patterns, and working buffers

### Command Structure
APU commands follow a similar structure to GPU commands:
```
1 byte: Command ID
1 byte: Command length (including ID and length bytes)
N bytes: Command parameters
```

Example APU commands:
```c
// Play sound effect
void play_sound_effect(uint8_t channel, uint8_t sample_id, uint8_t pitch, uint8_t volume) {
    uint8_t data[4] = {channel, sample_id, pitch, volume};
    queue_apu_command(0x71, 5, data);
}

// Start music track
void play_music(uint8_t tracker_id) {
    uint8_t data[1] = {tracker_id};
    queue_apu_command(0x11, 2, data);
}
```

## Inter-Microcontroller Communication
The three microcontrollers communicate through:

### Primary Communication
- **SPI Protocol**: CPU acts as master, GPU and APU as slaves
- **Command Queues**: Buffered commands for efficient processing
- **Synchronization Signals**: Using GPIO pins for VSYNC and timing

### Core-to-Core Communication
Each microcontroller also uses inter-core communication:
```c
// Message between CPU cores
typedef struct {
    MessageType type;
    uint32_t param1;
    uint32_t param2;
    void* data;
} CoreMessage;

// Inter-core queues
queue_t core0_to_core1_queue;
queue_t core1_to_core0_queue;
```

## Memory Allocation
Memory allocation is optimized for both RP2040 (264KB) and RP2350 (520KB) models:

### CPU Memory (RP2040/RP2350)
- Game code and state: 96KB/240KB
- Asset management buffers: 64KB/128KB
- Command queues: 48KB/96KB
- System variables and stack: 56KB/56KB

### GPU Memory (RP2040/RP2350)
- Framebuffer: 128KB/240KB (320×240×8-bit or higher on RP2350)
- Tile cache: 64KB/128KB
- Sprite data: 48KB/96KB
- Command buffer: 16KB/32KB
- Work RAM: 8KB/24KB

### APU Memory (RP2040/RP2350)
- Tracker pattern data: 64KB/128KB
- Sample storage: 128KB/256KB
- Instrument definitions: 32KB/64KB
- Working buffers: 40KB/72KB

## Game Development
The TriBoy platform provides high-level APIs for game development:

### CPU APIs
```c
// Sprite management
void set_sprite(uint8_t sprite_id, uint8_t pattern_id, int16_t x, int16_t y, uint8_t attributes);

// Background control
void scroll_background(uint8_t layer_id, int16_t x, int16_t y);

// Audio control
void play_sound_effect(uint8_t channel, uint8_t sample_id, uint8_t pitch, uint8_t volume);
void play_music(uint8_t tracker_id);
```

### Asset Management
```c
// Load assets from SD card
bool load_asset(AssetInfo* asset);

// Example asset info structure
typedef struct {
    uint32_t id;
    AssetType type;
    uint32_t size;
    uint32_t offset;
    bool loaded;
    uint8_t target; // 0=CPU, 1=GPU, 2=APU
} AssetInfo;
```

## Building a TriBoy
To build a TriBoy system, you'll need:

1. Three RP2040 (Raspberry Pi Pico) or Three RP2350 (Raspberry Pi Pico 2) microcontrollers
2. A display (SPI or parallel interface)
3. SD card reader for game storage
4. Audio amplifier and speaker/headphone jack
5. Input buttons/D-pad
6. Power management circuitry
7. Custom PCB to connect components

Hardware connections include:
- SPI buses between CPU-GPU and CPU-APU
- Display connection to GPU
- Audio output from APU
- Input buttons to CPU
- SD card to CPU
- Power distribution

## Potential Improvements
While the TriBoy design is comprehensive, there are several areas for potential improvements:

### Technical Improvements
1. **Unified Memory Architecture**: Implement a shared memory system between microcontrollers for larger assets
2. **Direct Memory Access**: Enable DMA transfers between microcontrollers for higher bandwidth
3. **Wireless Debugging**: Add Bluetooth or WiFi for wireless debugging and asset transfer
4. **Enhanced Power Management**: More sophisticated power modes for extended battery life
5. **Hardware Acceleration**: Additional PIO utilization for common graphics and audio operations

### Feature Additions
1. **Multiplayer Support**: Add communication interfaces for multiplayer gaming
2. **External Controller Support**: Standard interface for external controllers
3. **Expanded Storage**: Support for larger external storage beyond SD cards
4. **Development Toolchain**: Create comprehensive development tools, compilers, and asset converters
5. **Emulation Layer**: Add emulation capabilities for running existing 8/16-bit games

### Potential Issues to Address
1. **Communication Bottlenecks**: SPI bandwidth might limit complex graphics or fast scene changes
2. **Power Consumption**: Three microcontrollers will require efficient power management
3. **Synchronization Challenges**: Ensuring frame-perfect timing between CPU, GPU, and APU
4. **Memory Limitations**: Even with optimization, complex games may hit memory limits
5. **Development Complexity**: The three-processor architecture increases development complexity

### Architecture Alternatives
1. **Two-Microcontroller Design**: Combine GPU and APU functions for simplicity
2. **FPGA Acceleration**: Add a small FPGA for hardware acceleration of specific tasks
3. **Single RP2350 Implementation**: Leverage the increased capabilities of RP2350 for a single-chip solution
4. **External RAM**: Add external RAM chips to overcome memory limitations
