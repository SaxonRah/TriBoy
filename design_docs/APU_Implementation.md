# APU Command Implementation on RP2040/RP2350

Implementation details for the audio command set, covering the core functionality of the sound microcontroller for the TriBoy system, providing:

1. A comprehensive command processing system
2. Tracker music playback with multiple channels
3. PCM sample playback with pitch shifting
4. FM synthesis for rich instrumental sounds
5. Wavetable synthesis for custom waveforms
6. Audio effects processing pipeline
7. Memory management optimized for both RP2040 and RP2350

The implementation focuses on delivering high-quality 16-bit era sound while balancing performance and memory constraints.

---

## System Command Implementation

### NOP (0x00)
```c
void cmd_nop() {
    // No operation, used for padding or timing purposes
    return;
}
```

### Reset Audio (0x01)
```c
void cmd_reset_audio() {
    // Stop all active sounds
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channels[i].active = false;
        channels[i].volume = 0;
    }
    
    // Reset tracker state
    for (int i = 0; i < MAX_TRACKERS; i++) {
        trackers[i].playing = false;
        trackers[i].current_pattern = 0;
        trackers[i].current_row = 0;
    }
    
    // Reset effects
    reverb.room_size = 0;
    reverb.damping = 0;
    reverb.wet = 0;
    delay.time = 0;
    delay.feedback = 0;
    delay.wet = 0;
    
    // Clear sample memory
    memset(sample_data, 0, sizeof(sample_data));
    
    // Reset audio hardware
    setup_audio_hardware();
    
    // Reset master volume
    master_volume = DEFAULT_VOLUME;
    
    // Send acknowledgment to CPU
    send_ack_to_cpu(CMD_RESET_AUDIO);
}
```

### Set Master Volume (0x02)
```c
void cmd_set_master_volume(uint8_t volume) {
    // Set the global volume level
    master_volume = volume;
    
    // Apply volume scaling to output gain
    float normalized_volume = volume / 255.0f;
    
    // Apply volume curve for better perceived volume control
    // Square curve provides more natural volume control
    float scaled_volume = normalized_volume * normalized_volume;
    
    // Set hardware output gain
    set_audio_gain(scaled_volume);
}
```

### Get Status (0x03)
```c
void cmd_get_status() {
    // Prepare status packet
    uint8_t status_packet[8];
    
    // System status byte
    status_packet[0] = 0;
    if (audio_output_active) status_packet[0] |= 0x01;
    if (tracker_active) status_packet[0] |= 0x02;
    
    // Active channel count
    uint8_t active_channels = 0;
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].active) active_channels++;
    }
    status_packet[1] = active_channels;
    
    // Memory usage (percentage)
    status_packet[2] = (uint8_t)((memory_used * 100) / total_memory);
    
    // Current CPU load (0-255)
    status_packet[3] = cpu_load;
    
    // Error status
    status_packet[4] = error_code;
    
    // Reserve bytes for future use
    status_packet[5] = 0;
    status_packet[6] = 0;
    status_packet[7] = 0;
    
    // Send status packet to CPU
    send_data_to_cpu(status_packet, 8);
}
```

### Set Audio Config (0x04)
```c
void cmd_set_audio_config(uint8_t sample_rate_code, uint8_t bit_depth) {
    // Map sample rate code to actual frequency
    uint32_t sample_rates[] = {
        8000, 11025, 16000, 22050, 32000, 44100, 48000
    };
    
    uint32_t sample_rate = 44100; // Default
    if (sample_rate_code < sizeof(sample_rates)/sizeof(sample_rates[0])) {
        sample_rate = sample_rates[sample_rate_code];
    }
    
    // Configure audio hardware
    bool success = configure_audio_hardware(sample_rate, bit_depth);
    
    // Update system variables
    system_sample_rate = sample_rate;
    system_bit_depth = bit_depth;
    
    // Recalculate dependent values
    update_timing_constants();
    
    // Send acknowledgment
    send_status_to_cpu(success ? STATUS_OK : STATUS_ERROR_CONFIG);
}
```

### Sync Timing (0x05)
```c
void cmd_sync_timing(uint8_t tempo, uint8_t ticks_per_beat) {
    // Update tracker timing information
    global_tempo = tempo;
    global_ticks_per_beat = ticks_per_beat;
    
    // Calculate timing constants
    // (samples per tick based on sample rate and tempo)
    float beats_per_second = global_tempo / 60.0f;
    float ticks_per_second = beats_per_second * global_ticks_per_beat;
    samples_per_tick = (uint32_t)(system_sample_rate / ticks_per_second);
    
    // Reset tick counter
    current_tick = 0;
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SYNC_TIMING);
}
```

### Set Memory Mode (0x06)
```c
void cmd_set_memory_mode(uint8_t mode) {
    // Mode 0: Normal (RP2040), Mode 1: Expanded (RP2350)
    bool expanded_mode = (mode == 1);
    
    // Configure memory allocation based on available RAM
    if (expanded_mode) {
        // RP2350 configuration (520KB)
        sample_memory_size = 256 * 1024;  // 256KB for samples
        pattern_memory_size = 128 * 1024; // 128KB for patterns
        instrument_memory_size = 64 * 1024; // 64KB for instruments
        // Remaining ~72KB for working buffers
    } else {
        // RP2040 configuration (264KB)
        sample_memory_size = 128 * 1024;  // 128KB for samples
        pattern_memory_size = 64 * 1024;  // 64KB for patterns
        instrument_memory_size = 32 * 1024; // 32KB for instruments
        // Remaining ~40KB for working buffers
    }
    
    // Reallocate memory regions
    reallocate_memory();
    
    // Send acknowledgment
    send_ack_to_cpu(CMD_SET_MEMORY_MODE);
}
```

---

## Tracker Command Implementation

### Tracker Load (0x10)
```c
void cmd_tracker_load(uint8_t tracker_id, uint16_t data_size, uint8_t* data) {
    // Validate tracker ID
    if (tracker_id >= MAX_TRACKERS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Check if we have enough memory
    if (data_size > pattern_memory_available) {
        send_status_to_cpu(STATUS_ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Stop tracker if currently playing
    trackers[tracker_id].playing = false;
    
    // Parse header information
    uint8_t num_channels = data[0];
    uint8_t num_patterns = data[1];
    uint8_t num_instruments = data[2];
    uint8_t song_length = data[3];
    uint8_t default_tempo = data[4];
    
    // Store basic tracker info
    trackers[tracker_id].num_channels = min(num_channels, MAX_TRACKER_CHANNELS);
    trackers[tracker_id].num_patterns = num_patterns;
    trackers[tracker_id].song_length = song_length;
    trackers[tracker_id].tempo = default_tempo;
    trackers[tracker_id].current_pattern = 0;
    trackers[tracker_id].current_row = 0;
    
    // Copy pattern sequence
    memcpy(trackers[tracker_id].pattern_sequence, &data[5], song_length);
    
    // Calculate offset to pattern data
    uint16_t pattern_data_offset = 5 + song_length;
    
    // Allocate and copy pattern data
    trackers[tracker_id].pattern_data = allocate_pattern_memory(data_size - pattern_data_offset);
    if (trackers[tracker_id].pattern_data == NULL) {
        send_status_to_cpu(STATUS_ERROR_OUT_OF_MEMORY);
        return;
    }
    
    memcpy(trackers[tracker_id].pattern_data, &data[pattern_data_offset], 
           data_size - pattern_data_offset);
    
    // Update memory usage statistics
    pattern_memory_used += data_size - pattern_data_offset;
    pattern_memory_available -= data_size - pattern_data_offset;
    
    // Initialize playback state
    trackers[tracker_id].ticks_per_row = 6; // Default
    trackers[tracker_id].tick_counter = 0;
    trackers[tracker_id].loop_enabled = true;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Tracker Play (0x11)
```c
void cmd_tracker_play(uint8_t tracker_id) {
    // Validate tracker ID
    if (tracker_id >= MAX_TRACKERS || trackers[tracker_id].pattern_data == NULL) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Initialize playback state
    trackers[tracker_id].playing = true;
    trackers[tracker_id].current_pattern = 0;
    trackers[tracker_id].current_row = 0;
    trackers[tracker_id].tick_counter = 0;
    
    // Calculate first pattern to play
    uint8_t pattern = trackers[tracker_id].pattern_sequence[0];
    
    // Set up initial pattern pointer
    trackers[tracker_id].current_pattern_ptr = 
        get_pattern_pointer(tracker_id, pattern);
    
    // Process first row to start notes
    process_tracker_row(tracker_id);
    
    // Enable tracker processing in the audio callback
    tracker_active = true;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Tracker Stop (0x12)
```c
void cmd_tracker_stop(uint8_t tracker_id) {
    // Validate tracker ID
    if (tracker_id >= MAX_TRACKERS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Stop playback
    trackers[tracker_id].playing = false;
    
    // Stop all notes associated with this tracker
    for (int i = 0; i < trackers[tracker_id].num_channels; i++) {
        uint8_t channel_id = trackers[tracker_id].channel_map[i];
        if (channel_id < MAX_CHANNELS) {
            // Only stop if this channel belongs to this tracker
            if (channels[channel_id].owner_type == OWNER_TRACKER && 
                channels[channel_id].owner_id == tracker_id) {
                // Cut the sound
                channels[channel_id].active = false;
                channels[channel_id].volume = 0;
            }
        }
    }
    
    // Check if any trackers are still active
    tracker_active = false;
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (trackers[i].playing) {
            tracker_active = true;
            break;
        }
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Tracker Set Position (0x15)
```c
void cmd_tracker_set_position(uint8_t tracker_id, uint8_t pattern, uint8_t row) {
    // Validate tracker ID
    if (tracker_id >= MAX_TRACKERS || !trackers[tracker_id].pattern_data) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate pattern
    if (pattern >= trackers[tracker_id].song_length) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Get actual pattern number from sequence
    uint8_t actual_pattern = trackers[tracker_id].pattern_sequence[pattern];
    
    // Validate row
    uint8_t pattern_length = get_pattern_length(tracker_id, actual_pattern);
    if (row >= pattern_length) {
        row = pattern_length - 1;
    }
    
    // Update position
    trackers[tracker_id].position_in_sequence = pattern;
    trackers[tracker_id].current_row = row;
    trackers[tracker_id].tick_counter = 0;
    
    // Update pattern pointer
    trackers[tracker_id].current_pattern_ptr = 
        get_pattern_pointer(tracker_id, actual_pattern);
    
    // Advance pointer to the correct row
    trackers[tracker_id].current_pattern_ptr += 
        row * trackers[tracker_id].num_channels * TRACKER_NOTE_SIZE;
    
    // If playing, process the current row immediately
    if (trackers[tracker_id].playing) {
        process_tracker_row(tracker_id);
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## Channel Control Implementation

### Channel Set Volume (0x30)
```c
void cmd_channel_set_volume(uint8_t channel_id, uint8_t volume) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Update channel volume
    channels[channel_id].volume = volume;
    
    // Apply volume immediately if channel is active
    if (channels[channel_id].active) {
        // Different handling based on channel type
        switch (channels[channel_id].type) {
            case CHANNEL_TYPE_FM:
                update_fm_volume(channel_id);
                break;
                
            case CHANNEL_TYPE_SAMPLE:
                update_sample_volume(channel_id);
                break;
                
            case CHANNEL_TYPE_WAVETABLE:
                update_wavetable_volume(channel_id);
                break;
        }
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Channel Note On (0x33)
```c
void cmd_channel_note_on(uint8_t channel_id, uint8_t note, uint8_t velocity) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate note (0-127 MIDI range)
    if (note > 127) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Prepare channel
    channels[channel_id].active = true;
    channels[channel_id].note = note;
    channels[channel_id].velocity = velocity;
    
    // Calculate frequency from note
    // MIDI note to frequency conversion: 440Hz * 2^((note-69)/12)
    float freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    channels[channel_id].frequency = freq;
    
    // Different handling based on channel type
    switch (channels[channel_id].type) {
        case CHANNEL_TYPE_FM:
            start_fm_note(channel_id, freq);
            break;
            
        case CHANNEL_TYPE_SAMPLE:
            start_sample_note(channel_id, freq);
            break;
            
        case CHANNEL_TYPE_WAVETABLE:
            start_wavetable_note(channel_id, freq);
            break;
    }
    
    // Set ownership to direct control
    channels[channel_id].owner_type = OWNER_DIRECT;
    channels[channel_id].owner_id = 0;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Channel Set Effect (0x36)
```c
void cmd_channel_set_effect(uint8_t channel_id, uint8_t effect_type, uint8_t effect_value) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Process effect based on type
    switch (effect_type) {
        case EFFECT_VOLUME_SLIDE:
            // Set volume slide (0x0 to 0xF = slide down, 0xF0 to 0xFF = slide up)
            if (effect_value <= 0x0F) {
                channels[channel_id].vol_slide_down = effect_value;
                channels[channel_id].vol_slide_up = 0;
            } else {
                channels[channel_id].vol_slide_down = 0;
                channels[channel_id].vol_slide_up = (effect_value >> 4);
            }
            break;
            
        case EFFECT_PORTAMENTO:
            // Set portamento rate (pitch slide)
            channels[channel_id].portamento_rate = effect_value;
            break;
            
        case EFFECT_VIBRATO:
            // Set vibrato (depth in low nibble, speed in high nibble)
            channels[channel_id].vibrato_depth = effect_value & 0x0F;
            channels[channel_id].vibrato_speed = effect_value >> 4;
            break;
            
        case EFFECT_TREMOLO:
            // Set tremolo (depth in low nibble, speed in high nibble)
            channels[channel_id].tremolo_depth = effect_value & 0x0F;
            channels[channel_id].tremolo_speed = effect_value >> 4;
            break;
            
        case EFFECT_PAN_SLIDE:
            // Set pan slide (-7 to +7)
            channels[channel_id].pan_slide = (int8_t)(effect_value & 0x0F);
            if (channels[channel_id].pan_slide > 7) {
                channels[channel_id].pan_slide -= 16; // Convert to negative
            }
            break;
            
        case EFFECT_RETRIG:
            // Set retrigger count
            channels[channel_id].retrig_count = effect_value;
            channels[channel_id].retrig_counter = 0;
            break;
            
        case EFFECT_FINE_TUNE:
            // Set fine tuning (+/- 127 cents)
            channels[channel_id].fine_tune = (int8_t)effect_value;
            update_channel_frequency(channel_id);
            break;
            
        default:
            send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
            return;
    }
    
    // Enable effect processing for this channel
    channels[channel_id].effects_active |= (1 << effect_type);
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## FM Synthesis Implementation

### FM Init Channel (0x50)
```c
void cmd_fm_init_channel(uint8_t channel_id, uint8_t algorithm) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate algorithm (0-7 for 4-operator FM)
    if (algorithm > 7) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Configure channel as FM type
    channels[channel_id].type = CHANNEL_TYPE_FM;
    channels[channel_id].active = false;
    
    // Initialize FM parameters
    fm_channels[channel_id].algorithm = algorithm;
    fm_channels[channel_id].feedback = 0;
    
    // Reset all operators
    for (int i = 0; i < 4; i++) {
        FMOperator* op = &fm_channels[channel_id].operators[i];
        
        // Default values based on typical FM synth
        op->attack_rate = 31;     // Fast attack
        op->decay_rate = 20;      // Medium decay
        op->sustain_level = 10;   // Medium sustain
        op->release_rate = 15;    // Medium release
        op->total_level = 0;      // Maximum volume
        op->multiple = 1;         // Base frequency
        op->detune = 0;           // No detune
        op->waveform = 0;         // Sine wave
        
        // Initialize runtime state
        op->phase = 0;
        op->output = 0;
        op->envelope_level = 0;
        op->envelope_state = ENV_IDLE;
    }
    
    // Set algorithm-specific connections
    setup_fm_algorithm(channel_id, algorithm);
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### FM Set Operator (0x51)
```c
void cmd_fm_set_operator(uint8_t channel_id, uint8_t operator_id, uint8_t flags, 
                         uint8_t attack_rate, uint8_t decay_rate, uint8_t sustain_level, 
                         uint8_t release_rate, uint8_t waveform, uint8_t detune, 
                         uint8_t multiple) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS || channels[channel_id].type != CHANNEL_TYPE_FM) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate operator ID (0-3 for 4-operator FM)
    if (operator_id > 3) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Get operator pointer
    FMOperator* op = &fm_channels[channel_id].operators[operator_id];
    
    // Check if operator is enabled
    bool enabled = (flags & 0x01) != 0;
    
    // Update operator parameters
    op->attack_rate = attack_rate;
    op->decay_rate = decay_rate;
    op->sustain_level = sustain_level;
    op->release_rate = release_rate;
    
    // Check if waveform is valid (0-7 for different waveforms)
    if (waveform < 8) {
        op->waveform = waveform;
    }
    
    // Set detune (-3 to +3)
    op->detune = (int8_t)(detune & 0x07);
    if (op->detune > 3) {
        op->detune -= 8; // Convert to negative
    }
    
    // Set frequency multiple (0-15)
    op->multiple = multiple & 0x0F;
    
    // Set enabled state
    op->enabled = enabled;
    
    // If channel is active, update the sound immediately
    if (channels[channel_id].active) {
        // Recalculate operator frequencies
        update_fm_frequencies(channel_id);
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### FM Note On (0x52)
```c
void cmd_fm_note_on(uint8_t channel_id, uint8_t note, uint8_t velocity) {
    // Validate channel ID and type
    if (channel_id >= MAX_CHANNELS || channels[channel_id].type != CHANNEL_TYPE_FM) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate note (0-127 MIDI range)
    if (note > 127) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Set channel parameters
    channels[channel_id].active = true;
    channels[channel_id].note = note;
    channels[channel_id].velocity = velocity;
    
    // Calculate base frequency from note
    float freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    channels[channel_id].frequency = freq;
    
    // Start FM synthesis
    start_fm_note(channel_id, freq);
    
    // Set ownership to direct control
    channels[channel_id].owner_type = OWNER_DIRECT;
    channels[channel_id].owner_id = 0;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## Sample Playback Implementation

### Sample Load (0x70)
```c
void cmd_sample_load(uint8_t sample_id, uint8_t sample_format, uint16_t sample_rate,
                     uint16_t loop_start, uint16_t loop_end, uint16_t data_size, 
                     uint8_t* data) {
    // Validate sample ID
    if (sample_id >= MAX_SAMPLES) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Check available memory
    if (data_size > sample_memory_available) {
        send_status_to_cpu(STATUS_ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Free previous sample if it exists
    if (samples[sample_id].data != NULL) {
        sample_memory_available += samples[sample_id].size;
        free_sample_memory(samples[sample_id].data);
        samples[sample_id].data = NULL;
    }
    
    // Parse sample format
    bool is_16bit = (sample_format & 0x01) != 0;
    bool is_stereo = (sample_format & 0x02) != 0;
    
    // Allocate memory for the sample
    void* sample_memory = allocate_sample_memory(data_size);
    if (sample_memory == NULL) {
        send_status_to_cpu(STATUS_ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Copy sample data
    memcpy(sample_memory, data, data_size);
    
    // Configure sample
    samples[sample_id].data = sample_memory;
    samples[sample_id].size = data_size;
    samples[sample_id].format = sample_format;
    samples[sample_id].sample_rate = sample_rate;
    samples[sample_id].loop_start = loop_start;
    samples[sample_id].loop_end = loop_end;
    samples[sample_id].is_16bit = is_16bit;
    samples[sample_id].is_stereo = is_stereo;
    
    // Calculate bytes per sample for position calculations
    samples[sample_id].bytes_per_sample = (is_16bit ? 2 : 1) * (is_stereo ? 2 : 1);
    
    // Update memory tracking
    sample_memory_available -= data_size;
    sample_memory_used += data_size;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Sample Play (0x71)
```c
void cmd_sample_play(uint8_t channel_id, uint8_t sample_id, uint8_t pitch, uint8_t volume) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate sample ID and check if sample exists
    if (sample_id >= MAX_SAMPLES || samples[sample_id].data == NULL) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Configure channel as sample type
    channels[channel_id].type = CHANNEL_TYPE_SAMPLE;
    channels[channel_id].active = true;
    channels[channel_id].volume = volume;
    
    // Calculate pitch shift
    // pitch: 0-255 where 128 is original pitch, 64 is one octave down, 192 is one octave up
    float pitch_ratio = powf(2.0f, (pitch - 128) / 64.0f);
    
    // Set up sample playback
    sample_channels[channel_id].sample_id = sample_id;
    sample_channels[channel_id].position = 0;
    sample_channels[channel_id].position_frac = 0;
    sample_channels[channel_id].pitch_ratio = pitch_ratio;
    
    // Calculate the step size for sample position advancement
    float step = (float)samples[sample_id].sample_rate * pitch_ratio / (float)system_sample_rate;
    sample_channels[channel_id].step = step;
    
    // Set loop mode (default to no loop)
    sample_channels[channel_id].loop_mode = LOOP_NONE;
    
    // Start playback
    channels[channel_id].active = true;
    
    // Set ownership to direct control
    channels[channel_id].owner_type = OWNER_DIRECT;
    channels[channel_id].owner_id = 0;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Sample Loop Enable (0x73)
```c
void cmd_sample_loop_enable(uint8_t channel_id, uint8_t loop_mode) {
    // Validate channel ID and type
    if (channel_id >= MAX_CHANNELS || channels[channel_id].type != CHANNEL_TYPE_SAMPLE) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Get the sample ID
    uint8_t sample_id = sample_channels[channel_id].sample_id;
    
    // Check if the sample has loop points defined
    if (samples[sample_id].loop_start >= samples[sample_id].loop_end) {
        send_status_to_cpu(STATUS_ERROR_INVALID_OPERATION);
        return;
    }
    
    // Set loop mode
    switch (loop_mode) {
        case 0: // No loop
            sample_channels[channel_id].loop_mode = LOOP_NONE;
            break;
            
        case 1: // Forward loop
            sample_channels[channel_id].loop_mode = LOOP_FORWARD;
            break;
            
        case 2: // Ping-pong loop
            sample_channels[channel_id].loop_mode = LOOP_PINGPONG;
            break;
            
        default:
            send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
            return;
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## Wavetable Synthesis Implementation

### Wave Define Table (0x90)
```c
void cmd_wave_define_table(uint8_t table_id, uint8_t wave_size, uint8_t* data) {
    // Validate table ID
    if (table_id >= MAX_WAVETABLES) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate wave size (must be power of 2)
    uint16_t actual_size;
    if (wave_size == 0) {
        actual_size = 256; // Default size
    } else {
        // Round to nearest power of 2 (32, 64, 128, 256, 512)
        actual_size = 32;
        while (actual_size < wave_size && actual_size < 512) {
            actual_size *= 2;
        }
    }
    
    // Check if we need to free existing wavetable
    if (wavetables[table_id].data != NULL) {
        free_wavetable_memory(wavetables[table_id].data);
    }
    
    // Allocate memory for wavetable
    int16_t* wave_memory = allocate_wavetable_memory(actual_size * sizeof(int16_t));
    if (wave_memory == NULL) {
        send_status_to_cpu(STATUS_ERROR_OUT_OF_MEMORY);
        return;
    }
    
    // Convert 8-bit unsigned data to 16-bit signed
    for (int i = 0; i < actual_size; i++) {
        if (i < wave_size) {
            // Convert from 0-255 to -32768 to 32767
            wave_memory[i] = ((int16_t)data[i] - 128) * 256;
        } else {
            // Fill remaining space with zeros if input is smaller
            wave_memory[i] = 0;
        }
    }
    
    // Configure wavetable
    wavetables[table_id].data = wave_memory;
    wavetables[table_id].size = actual_size;
    wavetables[table_id].mask = actual_size - 1; // For fast indexing
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Wave Set Channel (0x91)
```c
void cmd_wave_set_channel(uint8_t channel_id, uint8_t table_id) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate wavetable ID and check if it exists
    if (table_id >= MAX_WAVETABLES || wavetables[table_id].data == NULL) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Configure channel as wavetable type
    channels[channel_id].type = CHANNEL_TYPE_WAVETABLE;
    
    // Assign wavetable to channel
    wave_channels[channel_id].table_id = table_id;
    wave_channels[channel_id].position = 0;
    wave_channels[channel_id].position_frac = 0;
    
    // Initialize wavetable-specific parameters
    wave_channels[channel_id].pulse_width = 128; // 50% duty cycle
    wave_channels[channel_id].mod_depth = 0;     // No modulation
    wave_channels[channel_id].mod_speed = 0;     // No modulation
    wave_channels[channel_id].mod_phase = 0;     // Initial phase
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Wave Note On (0x92)
```c
void cmd_wave_note_on(uint8_t channel_id, uint8_t note, uint8_t velocity) {
    // Validate channel ID and type
    if (channel_id >= MAX_CHANNELS || channels[channel_id].type != CHANNEL_TYPE_WAVETABLE) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate note (0-127 MIDI range)
    if (note > 127) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Set channel parameters
    channels[channel_id].active = true;
    channels[channel_id].note = note;
    channels[channel_id].velocity = velocity;
    
    // Calculate base frequency from note
    float freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    channels[channel_id].frequency = freq;
    
    // Start wavetable synthesis
    start_wavetable_note(channel_id, freq);
    
    // Set ownership to direct control
    channels[channel_id].owner_type = OWNER_DIRECT;
    channels[channel_id].owner_id = 0;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Wave Set Sweep (0x94)
```c
void cmd_wave_set_sweep(uint8_t channel_id, uint8_t start_table, uint8_t end_table, uint8_t sweep_rate) {
    // Validate channel ID and type
    if (channel_id >= MAX_CHANNELS || channels[channel_id].type != CHANNEL_TYPE_WAVETABLE) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate wavetable IDs
    if (start_table >= MAX_WAVETABLES || wavetables[start_table].data == NULL ||
        end_table >= MAX_WAVETABLES || wavetables[end_table].data == NULL) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Configure wavetable morphing
    wave_channels[channel_id].sweep_start_table = start_table;
    wave_channels[channel_id].sweep_end_table = end_table;
    wave_channels[channel_id].sweep_rate = sweep_rate;
    wave_channels[channel_id].sweep_position = 0;
    wave_channels[channel_id].sweep_active = true;
    
    // Ensure tables have compatible sizes
    if (wavetables[start_table].size != wavetables[end_table].size) {
        // Handle incompatible sizes (use the smaller of the two)
        wave_channels[channel_id].sweep_size = 
            min(wavetables[start_table].size, wavetables[end_table].size);
    } else {
        wave_channels[channel_id].sweep_size = wavetables[start_table].size;
    }
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## Effects Processing Implementation

### Effect Set Reverb (0xB0)
```c
void cmd_effect_set_reverb(uint8_t room_size, uint8_t damping, uint8_t wet) {
    // Configure global reverb parameters
    reverb.room_size = room_size;
    reverb.damping = damping;
    reverb.wet = wet;
    reverb.dry = 255 - wet; // Inverse of wet level
    
    // Pre-calculate coefficients for reverb algorithm
    float normalized_room = room_size / 255.0f;
    float normalized_damp = damping / 255.0f;
    float normalized_wet = wet / 255.0f;
    
    // Apply to Schroeder reverb parameters (simplified version)
    // These would be tuned for the specific implementation
    reverb.feedback = 0.7f + normalized_room * 0.28f; // 0.7 - 0.98 range
    reverb.lp_coeff = 1.0f - normalized_damp * 0.95f; // 1.0 - 0.05 range
    reverb.wet_gain = normalized_wet;
    reverb.dry_gain = 1.0f - normalized_wet * 0.5f; // Less aggressive dry reduction
    
    // Clear delay lines if parameters changed significantly
    if (abs(reverb.prev_room_size - room_size) > 50) {
        clear_reverb_buffers();
    }
    
    reverb.prev_room_size = room_size;
    
    // Enable reverb processing
    reverb.enabled = (wet > 0);
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Effect Set Delay (0xB1)
```c
void cmd_effect_set_delay(uint16_t delay_time, uint8_t feedback, uint8_t wet) {
    // Validate delay time (max depends on available memory)
    uint16_t max_delay_samples = delay_buffer_size / 2; // In stereo samples
    uint16_t delay_samples = min((delay_time * system_sample_rate) / 1000, max_delay_samples);
    
    // Configure delay parameters
    delay.time = delay_time;
    delay.samples = delay_samples;
    delay.feedback = feedback;
    delay.wet = wet;
    delay.dry = 255 - (wet / 2); // Less aggressive dry reduction
    
    // Calculate normalized parameters for processing
    delay.feedback_gain = feedback / 255.0f;
    delay.wet_gain = wet / 255.0f;
    delay.dry_gain = delay.dry / 255.0f;
    
    // If delay time changed significantly, clear buffer
    if (abs(delay.prev_samples - delay_samples) > system_sample_rate / 50) { // > 20ms difference
        memset(delay_buffer, 0, delay_buffer_size * sizeof(int16_t));
    }
    
    delay.prev_samples = delay_samples;
    delay.write_pos = 0;
    
    // Enable delay processing
    delay.enabled = (wet > 0);
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Effect Set Filter (0xB2)
```c
void cmd_effect_set_filter(uint8_t channel_id, uint8_t filter_type, uint8_t cutoff, uint8_t resonance) {
    // Validate channel ID
    if (channel_id >= MAX_CHANNELS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_ID);
        return;
    }
    
    // Validate filter type
    if (filter_type > FILTER_BANDPASS) {
        send_status_to_cpu(STATUS_ERROR_INVALID_PARAMETER);
        return;
    }
    
    // Configure filter parameters
    filters[channel_id].type = filter_type;
    filters[channel_id].cutoff = cutoff;
    filters[channel_id].resonance = resonance;
    
    // Calculate filter coefficients
    float normalized_cutoff = (cutoff / 255.0f) * 0.45f; // 0-0.45 range (avoid nyquist issues)
    float normalized_resonance = resonance / 255.0f;
    
    // Convert to filter coefficients (bilinear transform for digital implementation)
    switch (filter_type) {
        case FILTER_LOWPASS:
            calculate_lowpass_coefficients(channel_id, normalized_cutoff, normalized_resonance);
            break;
            
        case FILTER_HIGHPASS:
            calculate_highpass_coefficients(channel_id, normalized_cutoff, normalized_resonance);
            break;
            
        case FILTER_BANDPASS:
            calculate_bandpass_coefficients(channel_id, normalized_cutoff, normalized_resonance);
            break;
    }
    
    // Reset filter state
    filters[channel_id].x1 = 0;
    filters[channel_id].x2 = 0;
    filters[channel_id].y1 = 0;
    filters[channel_id].y2 = 0;
    
    // Enable filter
    filters[channel_id].enabled = true;
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

---

## Memory Management Implementation

### Mem Clear Samples (0xD0)
```c
void cmd_mem_clear_samples() {
    // Stop any channels that are playing samples
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].type == CHANNEL_TYPE_SAMPLE && channels[i].active) {
            channels[i].active = false;
        }
    }
    
    // Free all sample memory
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (samples[i].data != NULL) {
            free_sample_memory(samples[i].data);
            samples[i].data = NULL;
            samples[i].size = 0;
        }
    }
    
    // Reset memory tracking
    sample_memory_used = 0;
    sample_memory_available = sample_memory_size;
    
    // Defragment sample memory pool
    defragment_sample_memory();
    
    // Send acknowledgment
    send_status_to_cpu(STATUS_OK);
}
```

### Mem Status (0xD3)
```c
void cmd_mem_status() {
    // Prepare memory status packet
    uint8_t status_packet[16];
    
    // Total memory available
    uint32_t total_memory = sample_memory_size + pattern_memory_size + instrument_memory_size;
    
    // Calculate usage percentages
    uint8_t sample_usage = (uint8_t)((sample_memory_used * 100) / sample_memory_size);
    uint8_t pattern_usage = (uint8_t)((pattern_memory_used * 100) / pattern_memory_size);
    uint8_t instrument_usage = (uint8_t)((instrument_memory_used * 100) / instrument_memory_size);
    uint8_t total_usage = (uint8_t)(((sample_memory_used + pattern_memory_used + instrument_memory_used) * 100) / total_memory);
    
    // Fill in status packet
    // Overall memory usage
    status_packet[0] = total_usage;
    
    // Detailed memory usage
    status_packet[1] = sample_usage;
    status_packet[2] = pattern_usage;
    status_packet[3] = instrument_usage;
    
    // Available memory in KB
    status_packet[4] = (sample_memory_available / 1024) & 0xFF;
    status_packet[5] = (pattern_memory_available / 1024) & 0xFF;
    status_packet[6] = (instrument_memory_available / 1024) & 0xFF;
    
    // Largest contiguous blocks
    status_packet[7] = (largest_contiguous_sample_block() / 1024) & 0xFF;
    status_packet[8] = (largest_contiguous_pattern_block() / 1024) & 0xFF;
    status_packet[9] = (largest_contiguous_instrument_block() / 1024) & 0xFF;
    
    // Fragmentation indicators (0-255)
    status_packet[10] = calculate_sample_fragmentation();
    status_packet[11] = calculate_pattern_fragmentation();
    status_packet[12] = calculate_instrument_fragmentation();
    
    // Reserved for future use
    status_packet[13] = 0;
    status_packet[14] = 0;
    status_packet[15] = 0;
    
    // Send status packet to CPU
    send_data_to_cpu(status_packet, 16);
}
```

---

## Core Audio Processing Functions

### Audio Mixing Function
```c
void mix_audio_buffer(int16_t* output_buffer, uint32_t num_samples) {
    // Clear mixing buffer
    memset(mix_buffer, 0, num_samples * 2 * sizeof(int32_t)); // Stereo, 32-bit for headroom
    
    // Process all active channels
    for (int ch = 0; ch < MAX_CHANNELS; ch++) {
        if (!channels[ch].active) continue;
        
        // Process channel based on type
        switch (channels[ch].type) {
            case CHANNEL_TYPE_FM:
                process_fm_channel(ch, mix_buffer, num_samples);
                break;
                
            case CHANNEL_TYPE_SAMPLE:
                process_sample_channel(ch, mix_buffer, num_samples);
                break;
                
            case CHANNEL_TYPE_WAVETABLE:
                process_wavetable_channel(ch, mix_buffer, num_samples);
                break;
        }
    }
    
    // Apply global effects in order
    if (delay.enabled) {
        apply_delay_effect(mix_buffer, num_samples);
    }
    
    if (reverb.enabled) {
        apply_reverb_effect(mix_buffer, num_samples);
    }
    
    // Convert from 32-bit internal mix to 16-bit output
    // Apply master volume and clipping protection
    float master_gain = master_volume / 255.0f;
    for (uint32_t i = 0; i < num_samples * 2; i++) {
        // Scale by master volume
        int32_t sample = (int32_t)(mix_buffer[i] * master_gain);
        
        // Soft clipping (tanh approximation)
        if (sample > 32767 || sample < -32768) {
            float normalized = sample / 32768.0f;
            float clipped = normalized / (1.0f + fabsf(normalized));
            sample = (int32_t)(clipped * 32768.0f);
        }
        
        // Write to output buffer
        output_buffer[i] = (int16_t)sample;
    }
}
```

### Tracker Processing Function
```c
void process_tracker_tick() {
    // Check if any trackers are active
    if (!tracker_active) return;
    
    // Process each tracker
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (!trackers[i].playing) continue;
        
        // Increment tick counter
        trackers[i].tick_counter++;
        
        // Process row if it's time for a new row
        if (trackers[i].tick_counter >= trackers[i].ticks_per_row) {
            trackers[i].tick_counter = 0;
            
            // Process effects that happen on row change
            process_tracker_row(i);
            
            // Advance to next row
            trackers[i].current_row++;
            
            // Check if we reached the end of the pattern
            if (trackers[i].current_row >= trackers[i].rows_per_pattern) {
                trackers[i].current_row = 0;
                
                // Move to next pattern in sequence
                trackers[i].position_in_sequence++;
                
                // Check if we reached the end of the song
                if (trackers[i].position_in_sequence >= trackers[i].song_length) {
                    if (trackers[i].loop_enabled) {
                        // Loop back to beginning
                        trackers[i].position_in_sequence = 0;
                    } else {
                        // Stop playback
                        trackers[i].playing = false;
                        
                        // Stop all notes associated with this tracker
                        for (int ch = 0; ch < trackers[i].num_channels; ch++) {
                            uint8_t channel_id = trackers[i].channel_map[ch];
                            if (channel_id < MAX_CHANNELS) {
                                if (channels[channel_id].owner_type == OWNER_TRACKER && 
                                    channels[channel_id].owner_id == i) {
                                    channels[channel_id].active = false;
                                }
                            }
                        }
                        
                        continue;
                    }
                }
                
                // Get next pattern from sequence
                uint8_t next_pattern = trackers[i].pattern_sequence[trackers[i].position_in_sequence];
                
                // Update pattern pointer
                trackers[i].current_pattern_ptr = get_pattern_pointer(i, next_pattern);
            } else {
                // Advance pattern pointer to next row
                trackers[i].current_pattern_ptr += 
                    trackers[i].num_channels * TRACKER_NOTE_SIZE;
            }
        } else {
            // Process effects that happen on each tick
            process_tracker_tick_effects(i);
        }
    }
}
```

### FM Synthesis Processing
```c
void process_fm_channel(uint8_t channel_id, int32_t* mix_buffer, uint32_t num_samples) {
    // Get channel and FM-specific data
    Channel* ch = &channels[channel_id];
    FMChannel* fm = &fm_channels[channel_id];
    
    // Skip if not active
    if (!ch->active) return;
    
    // Get current pan and volume
    uint8_t volume = ch->volume;
    uint8_t pan = ch->pan;
    
    // Check if any channel effects are active
    if (ch->effects_active) {
        // Apply vibrato if active
        if (ch->effects_active & (1 << EFFECT_VIBRATO)) {
            apply_vibrato(channel_id);
        }
        
        // Apply tremolo if active
        if (ch->effects_active & (1 << EFFECT_TREMOLO)) {
            apply_tremolo(channel_id, &volume);
        }
    }
    
    // Calculate volume scaling
    float vol_left = volume * (255 - pan) / 65025.0f;  // 255*255
    float vol_right = volume * pan / 65025.0f;
    
    // Calculate base phase increment for all operators based on frequency
    uint32_t phase_inc = (uint32_t)(ch->frequency * FM_PHASE_PRECISION / system_sample_rate);
    
    // Process each sample
    for (uint32_t i = 0; i < num_samples; i++) {
        // Execute FM algorithm
        int16_t fm_sample = compute_fm_sample(channel_id, phase_inc);
        
        // Apply to stereo mix buffer
        mix_buffer[i*2] += (int32_t)(fm_sample * vol_left);
        mix_buffer[i*2+1] += (int32_t)(fm_sample * vol_right);
    }
    
    // Apply envelope processing - check if note should end
    bool note_active = update_fm_envelopes(channel_id);
    if (!note_active) {
        ch->active = false;
    }
}
```

### Sample Processing
```c
void process_sample_channel(uint8_t channel_id, int32_t* mix_buffer, uint32_t num_samples) {
    // Get channel and sample-specific data
    Channel* ch = &channels[channel_id];
    SampleChannel* sc = &sample_channels[channel_id];
    Sample* sample = &samples[sc->sample_id];
    
    // Skip if not active
    if (!ch->active || sample->data == NULL) return;
    
    // Get current pan and volume
    uint8_t volume = ch->volume;
    uint8_t pan = ch->pan;
    
    // Apply channel effects if active
    if (ch->effects_active) {
        // Apply vibrato if active (pitch modulation)
        if (ch->effects_active & (1 << EFFECT_VIBRATO)) {
            apply_vibrato(channel_id);
            // Update step based on new frequency
            float freq_ratio = ch->frequency / channels[channel_id].base_frequency;
            sc->step = sc->base_step * freq_ratio;
        }
        
        // Apply tremolo if active (volume modulation)
        if (ch->effects_active & (1 << EFFECT_TREMOLO)) {
            apply_tremolo(channel_id, &volume);
        }
        
        // Apply retrigger if active
        if (ch->effects_active & (1 << EFFECT_RETRIG)) {
            // Check if it's time to retrigger
            if (++ch->retrig_counter >= ch->retrig_count) {
                ch->retrig_counter = 0;
                sc->position = 0;
                sc->position_frac = 0;
            }
        }
    }
    
    // Calculate volume scaling (0-1.0 range, stereo)
    float vol_left = volume * (255 - pan) / 65025.0f;
    float vol_right = volume * pan / 65025.0f;
    
    // Determine if sample is 8-bit or 16-bit
    bool is_16bit = sample->is_16bit;
    bool is_stereo = sample->is_stereo;
    
    // Calculate sample end position and loop points in samples
    uint32_t sample_end = sample->size / sample->bytes_per_sample;
    uint32_t loop_start = sample->loop_start;
    uint32_t loop_end = sample->loop_end;
    
    // Make sure loop points are valid
    if (loop_end > sample_end) loop_end = sample_end;
    if (loop_start >= loop_end) loop_start = 0;
    
    // Process each output sample
    for (uint32_t i = 0; i < num_samples; i++) {
        // Check if we've reached the end of the sample
        if (sc->position >= sample_end) {
            if (sc->loop_mode == LOOP_NONE) {
                // End playback
                ch->active = false;
                break;
            } else if (sc->loop_mode == LOOP_FORWARD) {
                // Loop back to start
                sc->position = loop_start;
                sc->position_frac = 0;
            } else if (sc->loop_mode == LOOP_PINGPONG) {
                // Reverse direction
                sc->position = loop_end - 1;
                sc->position_frac = 0;
                sc->direction = -1;
            }
        } else if (sc->position < loop_start && sc->direction < 0 && sc->loop_mode == LOOP_PINGPONG) {
            // Reverse direction in ping-pong mode
            sc->position = loop_start;
            sc->position_frac = 0;
            sc->direction = 1;
        }
        
        // Read sample data with interpolation
        int16_t left_sample, right_sample;
        
        if (is_16bit) {
            if (is_stereo) {
                // 16-bit stereo sample
                int16_t* data = (int16_t*)sample->data;
                left_sample = interpolate_sample_16(data + sc->position * 2, sc->position_frac);
                right_sample = interpolate_sample_16(data + sc->position * 2 + 1, sc->position_frac);
            } else {
                // 16-bit mono sample
                int16_t* data = (int16_t*)sample->data;
                left_sample = right_sample = interpolate_sample_16(data + sc->position, sc->position_frac);
            }
        } else {
            if (is_stereo) {
                // 8-bit stereo sample
                uint8_t* data = (uint8_t*)sample->data;
                left_sample = interpolate_sample_8(data + sc->position * 2, sc->position_frac);
                right_sample = interpolate_sample_8(data + sc->position * 2 + 1, sc->position_frac);
            } else {
                // 8-bit mono sample
                uint8_t* data = (uint8_t*)sample->data;
                left_sample = right_sample = interpolate_sample_8(data + sc->position, sc->position_frac);
            }
        }
        
        // Apply volume and add to mix buffer
        mix_buffer[i*2] += (int32_t)(left_sample * vol_left);
        mix_buffer[i*2+1] += (int32_t)(right_sample * vol_right);
        
        // Advance sample position with pitch correction
        sc->position_frac += sc->step;
        while (sc->position_frac >= 1.0f) {
            sc->position_frac -= 1.0f;
            sc->position += sc->direction;
        }
    }
}
```

### Wavetable Synthesis Processing
```c
void process_wavetable_channel(uint8_t channel_id, int32_t* mix_buffer, uint32_t num_samples) {
    // Get channel and wavetable-specific data
    Channel* ch = &channels[channel_id];
    WaveChannel* wc = &wave_channels[channel_id];
    
    // Skip if not active
    if (!ch->active) return;
    
    // Get current wavetable
    uint8_t table_id = wc->table_id;
    if (table_id >= MAX_WAVETABLES || wavetables[table_id].data == NULL) {
        ch->active = false;
        return;
    }
    
    // Get current pan and volume
    uint8_t volume = ch->volume;
    uint8_t pan = ch->pan;
    
    // Apply channel effects if active
    if (ch->effects_active) {
        if (ch->effects_active & (1 << EFFECT_VIBRATO)) {
            apply_vibrato(channel_id);
        }
        
        if (ch->effects_active & (1 << EFFECT_TREMOLO)) {
            apply_tremolo(channel_id, &volume);
        }
    }
    
    // Calculate volume scaling
    float vol_left = volume * (255 - pan) / 65025.0f;
    float vol_right = volume * pan / 65025.0f;
    
    // Calculate wavetable size and mask (for fast indexing)
    uint16_t wave_size = wavetables[table_id].size;
    uint16_t wave_mask = wavetables[table_id].mask;
    
    // Calculate phase increment based on frequency
    float phase_inc = ch->frequency * wave_size / system_sample_rate;
    
    // Handle wavetable morphing if active
    int16_t* wave_data;
    int16_t* morph_data = NULL;
    float morph_factor = 0.0f;
    
    if (wc->sweep_active) {
        // Get source and target wavetables
        wave_data = wavetables[wc->sweep_start_table].data;
        morph_data = wavetables[wc->sweep_end_table].data;
        
        // Calculate morphing factor (0.0 - 1.0)
        morph_factor = wc->sweep_position / 255.0f;
        
        // Advance sweep position
        wc->sweep_position += wc->sweep_rate;
        if (wc->sweep_position > 255) {
            wc->sweep_position = 255;
            
            // Optional: swap start/end to allow oscillation
            if (wc->sweep_oscillate) {
                uint8_t temp = wc->sweep_start_table;
                wc->sweep_start_table = wc->sweep_end_table;
                wc->sweep_end_table = temp;
                wc->sweep_position = 0;
            }
        }
    } else {
        // Use single wavetable
        wave_data = wavetables[table_id].data;
    }
    
    // Process each sample
    for (uint32_t i = 0; i < num_samples; i++) {
        int16_t sample;
        
        if (wc->sweep_active && morph_data != NULL) {
            // Interpolate between two wavetables
            uint16_t pos1 = ((uint16_t)wc->position) & wave_mask;
            uint16_t pos2 = (pos1 + 1) & wave_mask;
            
            // Get samples from both wavetables
            int16_t sample1a = wave_data[pos1];
            int16_t sample1b = wave_data[pos2];
            int16_t sample2a = morph_data[pos1];
            int16_t sample2b = morph_data[pos2];
            
            // Interpolate within each wavetable
            float frac = wc->position - (int)wc->position;
            int16_t interp1 = (int16_t)(sample1a + (sample1b - sample1a) * frac);
            int16_t interp2 = (int16_t)(sample2a + (sample2b - sample2a) * frac);
            
            // Interpolate between wavetables
            sample = (int16_t)(interp1 + (interp2 - interp1) * morph_factor);
        } else {
            // Standard wavetable interpolation
            uint16_t pos1 = ((uint16_t)wc->position) & wave_mask;
            uint16_t pos2 = (pos1 + 1) & wave_mask;
            
            int16_t sample1 = wave_data[pos1];
            int16_t sample2 = wave_data[pos2];
            
            float frac = wc->position - (int)wc->position;
            sample = (int16_t)(sample1 + (sample2 - sample1) * frac);
        }
        
        // Apply to stereo mix buffer
        mix_buffer[i*2] += (int32_t)(sample * vol_left);
        mix_buffer[i*2+1] += (int32_t)(sample * vol_right);
        
        // Advance wavetable position
        wc->position += phase_inc;
        while (wc->position >= wave_size) {
            wc->position -= wave_size;
        }
    }
}
```

### Effects Processing Functions

```c
void apply_reverb_effect(int32_t* buffer, uint32_t num_samples) {
    // Simple Schroeder reverb implementation
    // For RP2040/RP2350, we use a simplified algorithm to save processing power
    float feedback = reverb.feedback;
    float lp_coeff = reverb.lp_coeff;
    
    for (uint32_t i = 0; i < num_samples; i++) {
        // Get current stereo sample
        int32_t left = buffer[i*2];
        int32_t right = buffer[i*2+1];
        
        // Mix to mono for reverb processing (saves CPU)
        int32_t mono_input = (left + right) / 2;
        
        // Apply comb filters in parallel
        int32_t comb1 = reverb_read(COMB1_OFFSET);
        int32_t comb2 = reverb_read(COMB2_OFFSET);
        int32_t comb3 = reverb_read(COMB3_OFFSET);
        int32_t comb4 = reverb_read(COMB4_OFFSET);
        
        // Apply lowpass filtering and feedback
        reverb.comb1_lp = (reverb.comb1_lp * lp_coeff) + ((1.0f - lp_coeff) * comb1);
        reverb.comb2_lp = (reverb.comb2_lp * lp_coeff) + ((1.0f - lp_coeff) * comb2);
        reverb.comb3_lp = (reverb.comb3_lp * lp_coeff) + ((1.0f - lp_coeff) * comb3);
        reverb.comb4_lp = (reverb.comb4_lp * lp_coeff) + ((1.0f - lp_coeff) * comb4);
        
        // Write back to comb filters
        reverb_write(COMB1_OFFSET, mono_input + (int32_t)(reverb.comb1_lp * feedback));
        reverb_write(COMB2_OFFSET, mono_input + (int32_t)(reverb.comb2_lp * feedback));
        reverb_write(COMB3_OFFSET, mono_input + (int32_t)(reverb.comb3_lp * feedback));
        reverb_write(COMB4_OFFSET, mono_input + (int32_t)(reverb.comb4_lp * feedback));
        
        // Sum comb outputs and apply allpass filters in series
        int32_t allpass_input = (comb1 + comb2 + comb3 + comb4) / 4;
        
        int32_t ap1 = reverb_read(AP1_OFFSET);
        int32_t ap1_out = ap1 - (allpass_input / 2);
        reverb_write(AP1_OFFSET, allpass_input + (ap1 / 2));
        
        int32_t ap2 = reverb_read(AP2_OFFSET);
        int32_t ap2_out = ap2 - (ap1_out / 2);
        reverb_write(AP2_OFFSET, ap1_out + (ap2 / 2));
        
        // Mix reverb output with dry signal
        buffer[i*2] = (int32_t)(left * reverb.dry_gain) + (int32_t)(ap2_out * reverb.wet_gain);
        buffer[i*2+1] = (int32_t)(right * reverb.dry_gain) + (int32_t)(ap2_out * reverb.wet_gain);
    }
}
```

```c
void apply_delay_effect(int32_t* buffer, uint32_t num_samples) {
    uint32_t delay_samples = delay.samples;
    float feedback_gain = delay.feedback_gain;
    float wet_gain = delay.wet_gain;
    float dry_gain = delay.dry_gain;
    
    for (uint32_t i = 0; i < num_samples; i++) {
        // Get current position
        uint32_t read_pos = (delay.write_pos + delay_buffer_size - delay_samples) % delay_buffer_size;
        
        // Read from delay buffer
        int16_t delay_l = delay_buffer[read_pos * 2];
        int16_t delay_r = delay_buffer[read_pos * 2 + 1];
        
        // Get current sample
        int32_t left = buffer[i*2];
        int32_t right = buffer[i*2+1];
        
        // Apply feedback
        int16_t new_l = (int16_t)(left + delay_l * feedback_gain);
        int16_t new_r = (int16_t)(right + delay_r * feedback_gain);
        
        // Write to delay buffer
        delay_buffer[delay.write_pos * 2] = new_l;
        delay_buffer[delay.write_pos * 2 + 1] = new_r;
        
        // Advance write position
        delay.write_pos = (delay.write_pos + 1) % delay_buffer_size;
        
        // Mix dry and delayed signals
        buffer[i*2] = (int32_t)(left * dry_gain) + (int32_t)(delay_l * wet_gain);
        buffer[i*2+1] = (int32_t)(right * dry_gain) + (int32_t)(delay_r * wet_gain);
    }
}
```

---

## Helper Functions

```c
// Calculate FM synthesis sample for a channel
int16_t compute_fm_sample(uint8_t channel_id, uint32_t phase_inc) {
    FMChannel* fm = &fm_channels[channel_id];
    
    // Apply algorithm-specific processing
    int32_t modulation = 0;
    int32_t output = 0;
    
    switch (fm->algorithm) {
        case 0: // Serial (1->2->3->4)
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            modulation = compute_operator_output(channel_id, 1, phase_inc, modulation);
            modulation = compute_operator_output(channel_id, 2, phase_inc, modulation);
            output = compute_operator_output(channel_id, 3, phase_inc, modulation);
            break;
            
        case 1: // 1->2->4, 3->4
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            modulation = compute_operator_output(channel_id, 1, phase_inc, modulation);
            
            int32_t mod2 = compute_operator_output(channel_id, 2, phase_inc, 0);
            
            output = compute_operator_output(channel_id, 3, phase_inc, modulation + mod2);
            break;
            
        case 2: // 1->4, 2->4, 3->4
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            int32_t mod3 = compute_operator_output(channel_id, 1, phase_inc, 0);
            int32_t mod4 = compute_operator_output(channel_id, 2, phase_inc, 0);
            
            output = compute_operator_output(channel_id, 3, phase_inc, modulation + mod3 + mod4);
            break;
            
        case 3: // 1->3, 2->3, 3->4
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            int32_t mod5 = compute_operator_output(channel_id, 1, phase_inc, 0);
            
            int32_t op3_out = compute_operator_output(channel_id, 2, phase_inc, modulation + mod5);
            output = compute_operator_output(channel_id, 3, phase_inc, op3_out);
            break;
            
        case 4: // 1->2, 3->4
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            int32_t op2_out = compute_operator_output(channel_id, 1, phase_inc, modulation);
            
            int32_t mod6 = compute_operator_output(channel_id, 2, phase_inc, 0);
            int32_t op4_out = compute_operator_output(channel_id, 3, phase_inc, mod6);
            
            output = op2_out + op4_out;
            break;
            
        case 5: // All operators in parallel
            int32_t op1_out = compute_operator_output(channel_id, 0, phase_inc, 0);
            int32_t op2_out2 = compute_operator_output(channel_id, 1, phase_inc, 0);
            int32_t op3_out2 = compute_operator_output(channel_id, 2, phase_inc, 0);
            int32_t op4_out2 = compute_operator_output(channel_id, 3, phase_inc, 0);
            
            output = op1_out + op2_out2 + op3_out2 + op4_out2;
            break;
            
        case 6: // 1->2, 3 and 4 independent
            modulation = compute_operator_output(channel_id, 0, phase_inc, 0);
            int32_t op2_out3 = compute_operator_output(channel_id, 1, phase_inc, modulation);
            int32_t op3_out3 = compute_operator_output(channel_id, 2, phase_inc, 0);
            int32_t op4_out3 = compute_operator_output(channel_id, 3, phase_inc, 0);
            
            output = op2_out3 + op3_out3 + op4_out3;
            break;
            
        case 7: // 1 with feedback, 2, 3, and 4 independent
            // Apply feedback from operator 1 to itself
            int32_t feedback_mod = (fm->feedback > 0) ? 
                ((fm->op1_prev1 + fm->op1_prev2) * fm->feedback) >> 1 : 0;
            
            int32_t op1_out2 = compute_operator_output(channel_id, 0, phase_inc, feedback_mod);
            int32_t op2_out4 = compute_operator_output(channel_id, 1, phase_inc, 0);
            int32_t op3_out4 = compute_operator_output(channel_id, 2, phase_inc, 0);
            int32_t op4_out4 = compute_operator_output(channel_id, 3, phase_inc, 0);
            
            // Store operator 1 output for feedback
            fm->op1_prev2 = fm->op1_prev1;
            fm->op1_prev1 = op1_out2;
            
            output = op1_out2 + op2_out4 + op3_out4 + op4_out4;
            break;
    }
    
    // Scale output to 16-bit range (prevent clipping)
    output = output / 4;
    
    // Clamp to 16-bit range
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    
    return (int16_t)output;
}
```

```c
// Apply vibrato effect to a channel
void apply_vibrato(uint8_t channel_id) {
    Channel* ch = &channels[channel_id];
    
    // Get vibrato parameters
    uint8_t depth = ch->vibrato_depth;
    uint8_t speed = ch->vibrato_speed;
    
    // LFO calculation (sine wave approximation)
    // Phase goes from 0 to 63 and then back to 0
    ch->vibrato_phase = (ch->vibrato_phase + speed) & 0x3F;
    
    // Generate sine wave value (-1 to +1)
    // This is a fast approximation using a table lookup
    float lfo_value = sine_table[ch->vibrato_phase];
    
    // Scale by depth (max ±1 semitone for depth=15)
    float cents = lfo_value * depth * 6.667f; // 100 cents = 1 semitone
    
    // Apply to frequency (2^(cents/1200) is the multiplier for cents)
    float multiplier = powf(2.0f, cents / 1200.0f);
    ch->frequency = ch->base_frequency * multiplier;
}
```

```c
// Process a row from the tracker
void process_tracker_row(uint8_t tracker_id) {
    Tracker* tr = &trackers[tracker_id];
    
    // Process each channel in this row
    for (int ch = 0; ch < tr->num_channels; ch++) {
        // Get row data for this channel
        uint8_t* note_data = tr->current_pattern_ptr + (ch * TRACKER_NOTE_SIZE);
        
        // Parse note data
        uint8_t note = note_data[0];
        uint8_t instrument = note_data[1];
        uint8_t volume = note_data[2];
        uint8_t effect = note_data[3];
        uint8_t effect_param = note_data[4];
        
        // Map tracker channel to actual audio channel
        uint8_t channel_id = tr->channel_map[ch];
        
        // Skip if no channel assigned
        if (channel_id >= MAX_CHANNELS) continue;
        
        // Process note
        if (note > 0 && note <= 96) { // 1-96 is valid note range
            // Calculate frequency (note 1 = C-0, 96 = B-7)
            float freq = 32.7032f * powf(2.0f, (note - 1) / 12.0f);
            
            // Load instrument if specified
            if (instrument > 0) {
                load_tracker_instrument(channel_id, instrument);
            }
            
            // Start note
            channels[channel_id].active = true;
            channels[channel_id].note = note;
            channels[channel_id].frequency = freq;
            channels[channel_id].base_frequency = freq;
            
            // Set ownership
            channels[channel_id].owner_type = OWNER_TRACKER;
            channels[channel_id].owner_id = tracker_id;
            
            // Trigger note based on channel type
            switch (channels[channel_id].type) {
                case CHANNEL_TYPE_FM:
                    start_fm_note(channel_id, freq);
                    break;
                    
                case CHANNEL_TYPE_SAMPLE:
                    start_sample_note(channel_id, freq);
                    break;
                    
                case CHANNEL_TYPE_WAVETABLE:
                    start_wavetable_note(channel_id, freq);
                    break;
            }
        } else if (note == 97) {
            // Note off
            channels[channel_id].active = false;
        }
        
        // Process volume
        if (volume > 0) {
            channels[channel_id].volume = volume;
        }
        
        // Process effect
        if (effect > 0) {
            process_tracker_effect(tracker_id, channel_id, effect, effect_param);
        }
    }
}
```

## Memory Management Functions

```c
// Allocate sample memory from the sample memory pool
void* allocate_sample_memory(uint32_t size) {
    // Implementation depends on memory management strategy
    // For simplicity, this example uses a basic first-fit allocation
    
    // Align size to 4-byte boundary
    size = (size + 3) & ~3;
    
    // Find a free block
    SampleMemoryBlock* block = sample_memory_free_list;
    SampleMemoryBlock* prev = NULL;
    
    while (block != NULL) {
        if (block->size >= size) {
            // Found a block big enough
            
            // Check if we should split the block
            if (block->size > size + sizeof(SampleMemoryBlock) + 16) {
                // Split the block
                SampleMemoryBlock* new_block = (SampleMemoryBlock*)((uint8_t*)block + sizeof(SampleMemoryBlock) + size);
                new_block->size = block->size - size - sizeof(SampleMemoryBlock);
                new_block->next = block->next;
                
                block->size = size;
                block->next = new_block;
            }
            
            // Remove block from free list
            if (prev == NULL) {
                sample_memory_free_list = block->next;
            } else {
                prev->next = block->next;
            }
            
            // Add to used list
            block->next = sample_memory_used_list;
            sample_memory_used_list = block;
            
            // Return pointer to data area
            return (uint8_t*)block + sizeof(SampleMemoryBlock);
        }
        
        prev = block;
        block = block->next;
    }
    
    // No suitable block found
    return NULL;
}
```

```c
// Free sample memory block
void free_sample_memory(void* ptr) {
    // Get block header (located before data)
    SampleMemoryBlock* block = (SampleMemoryBlock*)((uint8_t*)ptr - sizeof(SampleMemoryBlock));
    
    // Remove from used list
    SampleMemoryBlock* current = sample_memory_used_list;
    SampleMemoryBlock* prev = NULL;
    
    while (current != NULL) {
        if (current == block) {
            if (prev == NULL) {
                sample_memory_used_list = current->next;
            } else {
                prev->next = current->next;
            }
            break;
        }
        
        prev = current;
        current = current->next;
    }
    
    // Add to free list (with basic coalescing)
    SampleMemoryBlock* free_current = sample_memory_free_list;
    SampleMemoryBlock* free_prev = NULL;
    
    // Find position to insert (keep list sorted by address)
    while (free_current != NULL && free_current < block) {
        free_prev = free_current;
        free_current = free_current->next;
    }
    
    // Check if we can coalesce with previous block
    if (free_prev != NULL && 
        (uint8_t*)free_prev + sizeof(SampleMemoryBlock) + free_prev->size == (uint8_t*)block) {
        // Combine with previous block
        free_prev->size += sizeof(SampleMemoryBlock) + block->size;
        
        // Check if we can also coalesce with next block
        if (free_current != NULL && 
            (uint8_t*)block + sizeof(SampleMemoryBlock) + block->size == (uint8_t*)free_current) {
            free_prev->size += sizeof(SampleMemoryBlock) + free_current->size;
            free_prev->next = free_current->next;
        }
    } 
    // Check if we can coalesce with next block
    else if (free_current != NULL && 
             (uint8_t*)block + sizeof(SampleMemoryBlock) + block->size == (uint8_t*)free_current) {
        block->size += sizeof(SampleMemoryBlock) + free_current->size;
        block->next = free_current->next;
        
        if (free_prev == NULL) {
            sample_memory_free_list = block;
        } else {
            free_prev->next = block;
        }
    }
    // No coalescing possible, just insert
    else {
        if (free_prev == NULL) {
            block->next = sample_memory_free_list;
            sample_memory_free_list = block;
        } else {
            block->next = free_prev->next;
            free_prev->next = block;
        }
    }
}
```

---

# Performance Optimizations for RP2040/RP2350

## RP2040 (264KB) Specific Optimizations

1. **Memory Allocation Strategy**
   - Sample memory: 128KB fixed pool with custom allocator
   - Pattern memory: 64KB with compact note format (5 bytes per note)
   - Wavetable memory: Limit to 256-sample tables (512 bytes per table)
   - Effect buffer sizes: Shorter reverb decay and delay times

2. **Synthesis Optimizations**
   - Use fixed-point math for FM synthesis (Q16.16 format)
   - Implement simplified ADSR envelopes with lookup tables
   - Use integer-based interpolation for sample playback
   - Share sine tables between FM and LFO modules

3. **PIO Utilization**
   - Implement I2S/PWM audio output using one PIO block
   - Use DMA to feed audio data to PIO without CPU intervention
   - Single-buffered audio with careful timing

## RP2350 (520KB) Specific Optimizations

1. **Enhanced Memory Allocation**
   - Sample memory: 256KB with fragmentation management
   - Larger pattern buffer (128KB) for more complex music
   - Support for 512-sample wavetables (1KB per table)
   - Larger effect buffers for higher quality reverb

2. **Advanced Synthesis Features**
   - Full floating-point calculations for better audio quality
   - Support for more complex FM algorithms
   - Higher quality cubic interpolation for sample playback
   - Multi-point envelopes for more expressive sounds

3. **Multi-Core Processing**
   - Core 0: Command handling and main audio mixing
   - Core 1: Advanced effects processing and sample interpolation
   - Shared memory circular buffers for inter-core communication
   - Double-buffered audio output for smoother playback

4. **PIO Acceleration**
   - Implement 24-bit I2S audio using PIO for higher quality output
   - DMA chaining for continuous audio streaming
   - Hardware-accelerated sample rate conversion using PIO
