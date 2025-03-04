# TriBoy Audio Processing Unit (APU) Command Set
The command set provides an excellent balance between the authentic constraints of 16-bit era sound chips while leveraging the additional capabilities of the RP2040/RP2350 to enhance audio quality and flexibility.

## Implementation Notes

### Memory Allocation Strategy

**RP2040 (264KB)**:
- Tracker pattern data: 64KB
- Sample storage: 128KB
- Instrument definitions: 32KB
- Working buffers: 40KB

**RP2350 (520KB)**:
- Tracker pattern data: 128KB
- Sample storage: 256KB
- Instrument definitions: 64KB
- Working buffers: 72KB

### Feature Set Highlights

1. **Tracker System**
   - 16 simultaneous channels
   - 64 instruments per song
   - 128 patterns per song
   - Pattern length up to 256 rows
   - Effect commands similar to ProTracker/FastTracker 2

2. **Sample Playback**
   - 8-bit and 16-bit PCM sample support
   - Variable sample rates up to 44.1kHz
   - Forward and ping-pong looping
   - Pitch shifting without quality loss
   - Multiple concurrent samples

3. **FM Synthesis**
   - 4 operators per channel
   - 8 algorithms
   - Multiple waveforms per operator
   - LFO modulation for vibrato and tremolo
   - Feedback routing

4. **Wavetable Synthesis**
   - Custom definable wavetables
   - Morphing between tables
   - Position modulation
   - Phase manipulation

5. **Effects Processing**
   - Global reverb
   - Configurable delay/echo
   - Per-channel filters (low-pass, high-pass, band-pass)
   - Basic distortion
   - 3-band parametric EQ

## System Commands (0x00-0x0F)
```
0x00: NOP
Length: 1
Parameters: None
Description: No operation

0x01: RESET_AUDIO
Length: 1
Parameters: None
Description: Reset audio system to default state

0x02: SET_MASTER_VOLUME
Length: 2
Parameters: [volume:1] (0-255)
Description: Set master volume for all audio output

0x03: GET_STATUS
Length: 1
Parameters: None
Description: Request status packet from audio chip

0x04: SET_AUDIO_CONFIG
Length: 3
Parameters: [sampleRate:1] [bitDepth:1]
Description: Configure audio output parameters

0x05: SYNC_TIMING
Length: 3
Parameters: [tempo:1] [ticksPerBeat:1]
Description: Synchronize timing information with CPU

0x06: SET_MEMORY_MODE
Length: 2
Parameters: [mode:1] (0=Normal, 1=Expanded for RP2350)
Description: Configure memory allocation strategy
```

## Tracker Commands (0x10-0x2F)
```
0x10: TRACKER_LOAD
Length: Variable
Parameters: [trackerId:1] [dataSize:2] [data:n]
Description: Load tracker song data into specified slot

0x11: TRACKER_PLAY
Length: 2
Parameters: [trackerId:1]
Description: Start playback of loaded tracker song

0x12: TRACKER_STOP
Length: 2
Parameters: [trackerId:1]
Description: Stop tracker playback

0x13: TRACKER_PAUSE
Length: 2
Parameters: [trackerId:1]
Description: Pause tracker playback

0x14: TRACKER_RESUME
Length: 2
Parameters: [trackerId:1]
Description: Resume paused tracker playback

0x15: TRACKER_SET_POSITION
Length: 4
Parameters: [trackerId:1] [pattern:1] [row:1]
Description: Jump to specific position in song

0x16: TRACKER_SET_TEMPO
Length: 3
Parameters: [trackerId:1] [tempo:1]
Description: Change playback tempo (speed)

0x17: TRACKER_SET_LOOP
Length: 3
Parameters: [trackerId:1] [loopMode:1] (0=No loop, 1=Song loop, 2=Pattern loop)
Description: Configure loop behavior

0x18: TRACKER_SET_CHANNEL_MASK
Length: 4
Parameters: [trackerId:1] [channelMask:2]
Description: Enable/disable specific channels using bitmask

0x19: TRACKER_SET_PATTERN
Length: Variable
Parameters: [trackerId:1] [patternId:1] [dataSize:2] [data:n]
Description: Load individual pattern data

0x1A: TRACKER_SET_INSTRUMENT
Length: Variable
Parameters: [trackerId:1] [instrumentId:1] [dataSize:2] [data:n]
Description: Load instrument definition

0x1B: TRACKER_TRANSPOSE
Length: 3
Parameters: [trackerId:1] [semitones:1] (signed)
Description: Transpose entire song by semitones
```

## Channel Control Commands (0x30-0x4F)
```
0x30: CHANNEL_SET_VOLUME
Length: 3
Parameters: [channelId:1] [volume:1] (0-255)
Description: Set volume for specific channel

0x31: CHANNEL_SET_PAN
Length: 3
Parameters: [channelId:1] [pan:1] (0=Left, 128=Center, 255=Right)
Description: Set stereo panning for channel

0x32: CHANNEL_SET_PITCH
Length: 4
Parameters: [channelId:1] [pitchValue:2] (12-bit value)
Description: Set channel frequency/pitch directly

0x33: CHANNEL_NOTE_ON
Length: 4
Parameters: [channelId:1] [note:1] [velocity:1]
Description: Trigger note with velocity on channel

0x34: CHANNEL_NOTE_OFF
Length: 2
Parameters: [channelId:1]
Description: Stop currently playing note on channel

0x35: CHANNEL_SET_INSTRUMENT
Length: 3
Parameters: [channelId:1] [instrumentId:1]
Description: Assign instrument to channel

0x36: CHANNEL_SET_EFFECT
Length: 4
Parameters: [channelId:1] [effectType:1] [effectValue:1]
Description: Apply tracker-style effect to channel

0x37: CHANNEL_SET_ENVELOPE
Length: Variable
Parameters: [channelId:1] [envelopeType:1] [numPoints:1] [data:n*4]
Description: Set custom ADSR envelope for channel
```

## FM Synthesis Commands (0x50-0x6F)
```
0x50: FM_INIT_CHANNEL
Length: 3
Parameters: [channelId:1] [algorithm:1]
Description: Initialize channel for FM synthesis with algorithm

0x51: FM_SET_OPERATOR
Length: Variable
Parameters: [channelId:1] [operatorId:1] [flags:1] [attackRate:1] [decayRate:1] [sustainLevel:1] [releaseRate:1] [waveform:1] [detune:1] [multiple:1]
Description: Configure FM operator parameters

0x52: FM_NOTE_ON
Length: 4
Parameters: [channelId:1] [note:1] [velocity:1]
Description: Trigger FM note with velocity

0x53: FM_NOTE_OFF
Length: 2
Parameters: [channelId:1]
Description: Stop currently playing FM note

0x54: FM_SET_FEEDBACK
Length: 3
Parameters: [channelId:1] [feedback:1]
Description: Set feedback amount for FM channel

0x55: FM_SET_LFO
Length: 5
Parameters: [channelId:1] [lfoType:1] [rate:1] [depth:1]
Description: Configure LFO for frequency or amplitude modulation
```

## Sample Playback Commands (0x70-0x8F)
```
0x70: SAMPLE_LOAD
Length: Variable
Parameters: [sampleId:1] [sampleFormat:1] [sampleRate:2] [loopStart:2] [loopEnd:2] [dataSize:2] [data:n]
Description: Load PCM sample data into memory

0x71: SAMPLE_PLAY
Length: 5
Parameters: [channelId:1] [sampleId:1] [pitch:1] [volume:1]
Description: Play sample on specified channel

0x72: SAMPLE_STOP
Length: 2
Parameters: [channelId:1]
Description: Stop sample playback on channel

0x73: SAMPLE_LOOP_ENABLE
Length: 3
Parameters: [channelId:1] [loopMode:1] (0=No loop, 1=Forward, 2=Ping-pong)
Description: Enable/configure sample looping

0x74: SAMPLE_SET_POSITION
Length: 4
Parameters: [channelId:1] [position:2]
Description: Set playback position within sample

0x75: SAMPLE_SET_PITCH
Length: 4
Parameters: [channelId:1] [pitch:2]
Description: Adjust sample playback rate/pitch

0x76: SAMPLE_SET_REGION
Length: 5
Parameters: [channelId:1] [startOffset:2] [endOffset:2]
Description: Play specific portion of a sample
```

## Wavetable Synthesis Commands (0x90-0xAF)
```
0x90: WAVE_DEFINE_TABLE
Length: Variable
Parameters: [tableId:1] [waveSize:1] [data:n]
Description: Define custom wavetable (256 bytes for RP2040, up to 512 for RP2350)

0x91: WAVE_SET_CHANNEL
Length: 3
Parameters: [channelId:1] [tableId:1]
Description: Assign wavetable to channel

0x92: WAVE_NOTE_ON
Length: 4
Parameters: [channelId:1] [note:1] [velocity:1]
Description: Play note using wavetable synthesis

0x93: WAVE_NOTE_OFF
Length: 2
Parameters: [channelId:1]
Description: Stop wavetable note

0x94: WAVE_SET_SWEEP
Length: 5
Parameters: [channelId:1] [startTable:1] [endTable:1] [sweepRate:1]
Description: Morph between wavetables over time

0x95: WAVE_SET_POSITION
Length: 3
Parameters: [channelId:1] [position:1]
Description: Set starting position within wavetable
```

## Effects Processing Commands (0xB0-0xCF)
```
0xB0: EFFECT_SET_REVERB
Length: 4
Parameters: [roomSize:1] [damping:1] [wet:1]
Description: Configure global reverb effect

0xB1: EFFECT_SET_DELAY
Length: 5
Parameters: [delayTime:2] [feedback:1] [wet:1]
Description: Configure delay/echo effect

0xB2: EFFECT_SET_FILTER
Length: 5
Parameters: [channelId:1] [filterType:1] [cutoff:1] [resonance:1]
Description: Set filter parameters for channel (LP, HP, BP)

0xB3: EFFECT_SET_DISTORTION
Length: 3
Parameters: [channelId:1] [amount:1]
Description: Apply distortion effect to channel

0xB4: EFFECT_CHANNEL_ROUTING
Length: 3
Parameters: [channelId:1] [effectMask:1]
Description: Configure which effects apply to which channels

0xB5: EFFECT_SET_EQ
Length: 5
Parameters: [channelId:1] [eqBand:1] [frequency:1] [gain:1]
Description: Configure parametric EQ for channel
```

## Memory Management Commands (0xD0-0xEF)
```
0xD0: MEM_CLEAR_SAMPLES
Length: 1
Parameters: None
Description: Clear all sample data from memory

0xD1: MEM_CLEAR_INSTRUMENTS
Length: 1
Parameters: None
Description: Clear all instrument definitions

0xD2: MEM_CLEAR_PATTERNS
Length: 1
Parameters: None
Description: Clear all pattern data

0xD3: MEM_STATUS
Length: 1
Parameters: None
Description: Request memory utilization information

0xD4: MEM_OPTIMIZE
Length: 1
Parameters: None
Description: Reorganize memory for optimal usage

0xD5: MEM_SET_PRIORITY
Length: 3
Parameters: [resourceType:1] [priority:1]
Description: Set memory allocation priorities for different resource types
```
