# CPU Design for TriBoy System

## Overview
The design aligns with the  GPU and APU implementations, creating a cohesive three-microcontroller architecture that maximizes the capabilities of the RP2040/RP2350 while maintaining clear separation of concerns. The CPU will be the master controller in this three-microcontroller architecture, handling game logic, asset management, and coordinating communication between all components.

This CPU design creates a powerful yet streamlined architecture that:
1. Effectively divides work between two cores, with game logic on Core 0 and system management on Core 1
2. Provides a clean asset loading system that utilizes the SD card primarily during initialization
3. Manages communication with both the GPU and APU using efficient command queues
4. Implements a QSPI loading mechanism that allows larger assets to be stored in external memory
5. Creates simple, high-level APIs for game developers to interact with the system


## Hardware Architecture

### Core Components
- **Microcontroller**: RP2040 (264KB RAM) or RP2350 (520KB RAM)
- **Storage**: SD card via SPI interface
- **Communication**: SPI master interfaces to both GPU and APU
- **User Input**: Button/D-pad inputs via GPIO with debouncing
- **Debug Interface**: Optional UART/USB for development

### Memory Allocation

**RP2040 Configuration (264KB)**:
- Game code and state: 96KB
- Asset management buffers: 64KB
- Command queues: 24KB for GPU, 24KB for APU
- System variables and stack: 56KB

**RP2350 Configuration (520KB)**:
- Game code and state: 240KB
- Asset management buffers: 128KB
- Command queues: 48KB for GPU, 48KB for APU
- System variables and stack: 56KB

### External Connections
- **SD Card**: CS, MOSI, MISO, SCK pins
- **GPU SPI**: CS, MOSI, MISO, SCK pins, plus VSYNC interrupt line
- **APU SPI**: CS, MOSI, MISO, SCK pins, plus audio sync line
- **Input**: 8-12 GPIO pins for buttons, D-pad, and other controls
- **Power Management**: GPIO for power control, battery monitoring

## Core Functionality

### Core 0: Game Logic
- Main game loop and game-specific code
- Physics calculations and collision detection
- Game state management
- Input processing and debouncing
- Game-specific AI and behavior logic

### Core 1: System Management
- Asset loading and memory management
- Communication with GPU and APU
- Command queue management and prioritization
- Synchronization between components
- Background tasks (loading next level assets, etc.)
