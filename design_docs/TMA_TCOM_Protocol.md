# TCOM: TriBoy Command Orchestration Mechanism

## TCOM Design Document v0.1

### 1. Overview

The TriBoy Command Orchestration Mechanism (TCOM) is a lightweight, high-performance communication protocol designed specifically for the Three Microcontroller Architecture (TMA) of the TriBoy gaming system. TCOM enables efficient command-based communication between the CPU, GPU, and APU microcontrollers, optimizing for both bandwidth utilization and processing efficiency.

---

### 2. Protocol Fundamentals

#### 2.1 Communication Model

TCOM implements a master-slave architecture:
- The CPU acts as the master device
- The GPU and APU act as slave devices
- Communication occurs primarily over SPI buses
- Additional control signals provide synchronization

#### 2.2 Basic Command Structure

All TCOM commands follow a consistent structure:

```
+------------+------------+------------------------+
| Command ID | Length     | Command Data           |
| (1 byte)   | (1 byte)   | (0 to 254 bytes)       |
+------------+------------+------------------------+
```

- **Command ID**: Identifies the specific command (0x00-0xFF)
- **Length**: Total length of the command including Command ID and Length fields
- **Command Data**: Variable-length payload specific to each command

#### 2.3 Physical Layer

TCOM operates over:
- Two dedicated SPI buses (CPU→GPU, CPU→APU)
- Control signal lines for synchronization and notifications
- Maximum SPI clock of 100MHz (at 200MHz CPU)

---

### 3. Signal Definitions

#### 3.1 SPI Bus Signals

| Signal Name | Direction | Description |
|-------------|-----------|-------------|
| SCK         | CPU → GPU/APU | SPI Clock |
| MOSI        | CPU → GPU/APU | Master Out, Slave In (data from CPU) |
| MISO        | GPU/APU → CPU | Master In, Slave Out (data to CPU) |
| CS          | CPU → GPU/APU | Chip Select (active low) |

#### 3.2 Control Signals

| Signal Name     | Direction     | Description |
|-----------------|---------------|-------------|
| DATA_READY      | GPU/APU → CPU | Indicates slave has data to send (active high) |
| VSYNC           | GPU → CPU     | Vertical sync signal (falling edge) |
| AUDIO_SYNC      | APU → CPU     | Audio timing synchronization (optional) |

---

### 4. Transaction Types

TCOM defines three primary transaction types:

#### 4.1 Command Transaction (CPU to GPU/APU)

1. CPU asserts CS (low)
2. CPU transmits Command ID, Length, and Data
3. CPU deasserts CS (high)
4. GPU/APU processes the command

#### 4.2 Acknowledgment Transaction (GPU/APU to CPU)

1. GPU/APU raises DATA_READY
2. CPU asserts CS (low)
3. GPU/APU transmits ACK packet
4. CPU deasserts CS (high)
5. GPU/APU lowers DATA_READY

#### 4.3 Notification Transaction (GPU/APU to CPU)

1. GPU/APU raises DATA_READY or pulses VSYNC
2. CPU recognizes the signal and asserts CS (low)
3. GPU/APU transmits notification packet
4. CPU deasserts CS (high)
5. GPU/APU lowers DATA_READY

---

### 5. Command Batching

To maximize efficiency, TCOM supports command batching:

#### 5.1 Batch Command Structure

```
+----------------+----------------+------------------+------------------------+
| Batch Command  | Command Count  | Total Data Size  | Command Data           |
| (1 byte)       | (1 byte)       | (2 bytes)        | (variable)             |
+----------------+----------------+------------------+------------------------+
```

Followed by multiple commands:
```
+------------+------------+------------------------+
| Command 1  | Length 1   | Data 1                 |
+------------+------------+------------------------+
| Command 2  | Length 2   | Data 2                 |
+------------+------------+------------------------+
| ...        | ...        | ...                    |
+------------+------------+------------------------+
```

#### 5.2 Batch Types

| Batch Type      | Command ID | Description |
|-----------------|------------|-------------|
| SPRITE_BATCH    | 0xB0       | Batched sprite operations |
| TILE_BATCH      | 0xB1       | Batched tile/layer operations |
| DRAW_BATCH      | 0xB2       | Batched drawing operations |
| AUDIO_BATCH     | 0xB3       | Batched audio operations |
| CHANNEL_BATCH   | 0xB4       | Batched channel operations |

### 6. Timing Specifications

#### 6.1 Key Timing Parameters

| Parameter | Symbol | Value | Description |
|-----------|--------|-------|-------------|
| SPI Clock Frequency | f_SPI | ≤100 MHz | Maximum SPI clock frequency |
| Chip Select Setup Time | t_CSS | ≥50 ns | Time between CS assertion and first clock edge |
| Chip Select Hold Time | t_CSH | ≥50 ns | Time between last clock edge and CS deassertion |
| Inter-Transaction Gap | t_ITG | ≥1 μs | Minimum time between transactions |
| DATA_READY Assert Time | t_DRA | ≥100 ns | Time for DATA_READY to be recognized |
| VSYNC Pulse Width | t_VPW | ≥1 μs | Duration of VSYNC pulse |
| Command Processing Timeout | t_CPT | ≤500 μs | Maximum time for command processing |

#### 6.2 Transaction Timing

For a typical 5-byte command at 100MHz SPI clock:
- Byte transfer time: 80 ns
- 5-byte command transfer: 400 ns
- Total transaction time with overhead: ~1.4 μs

---

### 7. Timing Diagrams

#### 7.1 Command Transaction (CPU to GPU/APU)

```
      t_CSS                   t_CSH
     <------>                <------>
CS    _______                        _______________________
             \______________________/

SCK   _______/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_____

MOSI  =======X===X===X===X===X===X===X===X===X===X=======
           CMD LEN  D1  D2  D3  ...

MISO  =======================================================
```

#### 7.2 Acknowledgment Transaction (GPU/APU to CPU)

```
DATA_READY   _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\________
                    |                          |
                    |<---- t_DRA ---->|        |
                                      |        |
CS          __________________________|‾‾‾‾‾‾‾‾|__________
                                               |
                                               |
SCK         ____________________________/‾\_/‾\_/‾\_/‾\___

MOSI        ==========================================

MISO        =============================X===X===X===X===
                                        ACK LEN CMD STS
```

#### 7.3 VSYNC Notification

```
VSYNC        _______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\________
                    |<-------- t_VPW -------->|

                    |                         |
                    V                         V
               Frame Start               Frame End
```

#### 7.4 Command Batch Transaction

```
CS    _______                                     ______
             \___________________________________/

SCK   _______/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾...\_/‾\_/‾\_____

MOSI  =======X===X===X===X===X===X===X...===X===X=======
           BCM CNT SZ1 SZ2 CM1 LN1 D11...

MISO  =======================================================

      |<----------------- Batch ---------------->|
```

---

### 8. Error Handling

#### 8.1 Error Codes

| Error Code | Value | Description |
|------------|-------|-------------|
| ERR_NONE | 0x00 | Success (no error) |
| ERR_TIMEOUT | 0x01 | Operation timed out |
| ERR_INVALID_COMMAND | 0x02 | Unknown command |
| ERR_INVALID_PARAMS | 0x03 | Invalid parameters |
| ERR_BUSY | 0x04 | Receiver busy |
| ERR_OUT_OF_MEMORY | 0x05 | Memory allocation failed |
| ERR_INVALID_STATE | 0x06 | Invalid state for operation |

#### 8.2 Acknowledgment Packet Structure

```
+------------+------------+------------------+------------+
| ACK CMD ID | Length     | Original CMD ID  | Error Code |
| (0xFA)     | (4)        | (1 byte)         | (1 byte)   |
+------------+------------+------------------+------------+
```

---

### 9. Synchronization Mechanisms

#### 9.1 Frame Synchronization

GPU generates VSYNC signals to indicate frame boundaries. The CPU can either:
1. Use the GPIO VSYNC signal (interrupt-driven)
2. Enable SPI VSYNC notifications (poll-driven)
3. Wait for VSYNC using a specific command (blocking)

#### 9.2 Audio Synchronization

For timing-critical audio:
1. CPU can send SYNC_TIMING command with tempo information
2. APU can signal audio events via DATA_READY + notification

#### 9.3 CPU-GPU-APU Coordination

For effects requiring coordination (e.g., music-synchronized visuals):
1. CPU sets up timing parameters on both GPU and APU
2. CPU acts as timing coordinator between systems
3. Timestamp-based events can be scheduled

---

### 10. Implementation Guidelines

#### 10.1 Command Prioritization

Commands should be prioritized in this order:
1. Timing-critical synchronization commands
2. Object/sprite movement commands
3. Audio trigger commands
4. Asset loading commands

#### 10.2 Interrupt Handling

- VSYNC should trigger high-priority interrupts
- DATA_READY can use lower-priority interrupts or polling
- Command processing should occur in dedicated tasks/threads

#### 10.3 Performance Optimization

- Batch similar commands whenever possible
- Use dirty rectangle tracking to minimize updates
- Process commands in Core 1 while Core 0 handles game logic
- Use DMA for large data transfers when available

---

### 11. Example Protocol Sequences

#### 11.1 Frame Rendering Sequence

```
APU                        CPU                       GPU
 |                          |                         |
 |                          |-- SPRITE_BATCH -------->|
 |                          |                         |
 |                          |-- LAYER_BATCH --------->|
 |                          |                         |
 |                          |-- EFFECT_SET ---------->|
 |                          |                         |
 |                          |<-- ACK -----------------|
 |                          |                         |
 |<-- AUDIO_BATCH ----------|                         |
 |                          |                         |
 |-- ACK ------------------->                         |
 |                          |                         |
 |                          |<------ VSYNC -----------|
 |                          |                         |
 |                          |-- Next Frame Commands ->|
 |                          |                         |
```

#### 11.2 Asset Loading Sequence

```
APU                        CPU                       GPU
 |                          |                         |
 |                          |-- LOAD_TILESET -------->|
 |                          |   (multiple chunks)     |
 |                          |                         |
 |                          |<-- ACK -----------------|
 |                          |                         |
 |                          |-- LOAD_SPRITE_PATTERN ->|
 |                          |                         |
 |                          |<-- ACK -----------------|
 |                          |                         |
 |<-- SAMPLE_LOAD ----------|                         |
 |   (multiple chunks)      |                         |
 |                          |                         |
 |-- ACK ------------------->                         |
 |                          |                         |
```

### 12. Conclusion

The TriBoy Command Orchestration Mechanism (TCOM) provides a robust, efficient communication framework for the TMA architecture. By combining low-level timing efficiency with high-level command abstraction, TCOM enables the TriBoy to deliver performance comparable to 16-bit era consoles while using modern microcontroller hardware.

The protocol prioritizes:
- Minimal overhead (only 2 bytes per command)
- Flexible command batching for efficiency
- Reliable synchronization between processors
- Clear error handling and recovery mechanisms

These features ensure that the inter-processor communication will not become a bottleneck, even for complex scenes and high frame rates, making the TriBoy platform both powerful and developer-friendly.
