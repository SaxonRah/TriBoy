/*
triboy_commands.h
Command definitions for the TriBoy Three Microcontroller Architecture (TMA)
This file defines all command IDs used for inter-microcontroller communication between the CPU, GPU, and APU components of the TriBoy system.
*/

#ifndef TRIBOY_COMMANDS_H
#define TRIBOY_COMMANDS_H

/*====================================================================================================================*/

/* GPU System Commands (0x00-0x0F) */
#define GPU_CMD_NOP                      0x00 /* No operation */
#define GPU_CMD_RESET_GPU                0x01 /* Reset GPU state to defaults */
#define GPU_CMD_SET_DISPLAY_MODE         0x02 /* Configure display resolution and color depth */
#define GPU_CMD_SET_VBLANK_CALLBACK      0x03 /* Toggle VBLANK interrupt signal to CPU */
#define GPU_CMD_VSYNC_WAIT               0x04 /* Notify CPU when next VBLANK occurs */
#define GPU_CMD_GET_STATUS               0x05 /* Return GPU status flags */
#define GPU_CMD_SET_POWER_MODE           0x06 /* Set GPU power mode (low power, standard, high performance) - Not in original spec */
#define GPU_CMD_SET_DEBUG_MODE           0x07 /* Toggle debug visualization modes - Not in original spec */
#define GPU_CMD_SET_FRAMERATE            0x08 /* Set target framerate - Not in original spec */
#define GPU_CMD_CLEAR_SCREEN             0x09 /* Clear screen to a specified color - Not in original spec */

/* GPU Palette Commands (0x10-0x1F) */
#define GPU_CMD_SET_PALETTE_ENTRY        0x10 /* Set single palette entry */
#define GPU_CMD_LOAD_PALETTE             0x11 /* Load multiple palette entries */
#define GPU_CMD_SET_TRANSPARENT_COLOR    0x12 /* Set transparent color index for sprites */
#define GPU_CMD_FADE_PALETTE             0x13 /* Animate palette fade over multiple frames - Not in original spec */
#define GPU_CMD_CYCLE_PALETTE            0x14 /* Set up palette cycling animation - Not in original spec */
#define GPU_CMD_BACKUP_PALETTE           0x15 /* Store current palette for later use - Not in original spec */
#define GPU_CMD_RESTORE_PALETTE          0x16 /* Restore previously backed up palette - Not in original spec */

/* GPU Background Layer Commands (0x20-0x3F) */
#define GPU_CMD_CONFIGURE_LAYER          0x20 /* Configure background layer properties */
#define GPU_CMD_LOAD_TILESET             0x21 /* Load tile graphics data */
#define GPU_CMD_LOAD_TILEMAP             0x22 /* Load tile mapping data */
#define GPU_CMD_SCROLL_LAYER             0x23 /* Set layer scroll position */
#define GPU_CMD_SET_HSCROLL_TABLE        0x24 /* Set per-line horizontal scroll values */
#define GPU_CMD_SET_VSCROLL_TABLE        0x25 /* Set per-column vertical scroll values */
#define GPU_CMD_SET_LAYER_PRIORITY       0x26 /* Change layer rendering priority - Not in original spec */
#define GPU_CMD_SET_LAYER_VISIBILITY     0x27 /* Toggle layer visibility - Not in original spec */
#define GPU_CMD_CLEAR_LAYER              0x28 /* Clear layer to a specified tile - Not in original spec */
#define GPU_CMD_UPDATE_TILE              0x29 /* Update a single tile in a tileset - Not in original spec */
#define GPU_CMD_COPY_LAYER_REGION        0x2A /* Copy a region from one layer to another - Not in original spec */
#define GPU_CMD_FILL_LAYER_REGION        0x2B /* Fill a region with a specified tile - Not in original spec */

/* GPU Sprite Commands (0x40-0x5F) */
#define GPU_CMD_LOAD_SPRITE_PATTERN      0x40 /* Load sprite pattern/graphic data */
#define GPU_CMD_DEFINE_SPRITE            0x41 /* Define sprite properties */
#define GPU_CMD_MOVE_SPRITE              0x42 /* Update sprite position */
#define GPU_CMD_SET_SPRITE_ATTRIBUTES    0x43 /* Update sprite attributes */
#define GPU_CMD_HIDE_SPRITE              0x44 /* Hide a sprite */
#define GPU_CMD_SHOW_SPRITE              0x45 /* Show a previously hidden sprite */
#define GPU_CMD_ANIMATE_SPRITE           0x46 /* Set up sprite animation */
#define GPU_CMD_SET_SPRITE_PRIORITY      0x47 /* Change sprite rendering priority - Not in original spec */
#define GPU_CMD_ROTATE_SPRITE            0x48 /* Rotate a sprite by specified angle - Not in original spec */
#define GPU_CMD_SCALE_SPRITE             0x49 /* Set sprite scaling factor - Not in original spec */
#define GPU_CMD_GET_SPRITE_COLLISION     0x4A /* Check if sprite collided with another sprite - Not in original spec */
#define GPU_CMD_BATCH_SPRITE_UPDATE      0x4B /* Update multiple sprites in a single command - Not in original spec */
#define GPU_CMD_SET_SPRITE_Z_DEPTH       0x4C /* Set sprite depth for 3D-like layering - Not in original spec */

/* GPU Special Effects Commands (0x60-0x7F) */
#define GPU_CMD_SET_FADE                 0x60 /* Set screen fade level */
#define GPU_CMD_MOSAIC_EFFECT            0x61 /* Apply mosaic pixelation effect */
#define GPU_CMD_SCANLINE_EFFECT          0x62 /* Apply per-scanline effects */
#define GPU_CMD_ROTATION_ZOOM_BACKGROUND 0x63 /* Apply affine transformation to background */
#define GPU_CMD_SET_WINDOW               0x64 /* Define rectangular window for clipping */
#define GPU_CMD_COLOR_MATH               0x65 /* Set blending mode between layers */
#define GPU_CMD_SET_BLUR                 0x66 /* Apply blur effect to screen - Not in original spec */
#define GPU_CMD_SET_NOISE                0x67 /* Add noise effect to screen - Not in original spec */
#define GPU_CMD_SHAKE_SCREEN             0x68 /* Screen shake effect - Not in original spec */
#define GPU_CMD_FLASH_SCREEN             0x69 /* Screen flash effect - Not in original spec */
#define GPU_CMD_APPLY_LUT                0x6A /* Apply color lookup table - Not in original spec */

/* GPU Direct Drawing Commands (0x80-0x9F) */
#define GPU_CMD_DRAW_PIXEL               0x80 /* Draw single pixel */
#define GPU_CMD_DRAW_LINE                0x81 /* Draw line between two points */
#define GPU_CMD_DRAW_RECT                0x82 /* Draw rectangle */
#define GPU_CMD_DRAW_CIRCLE              0x83 /* Draw circle */
#define GPU_CMD_BLIT_REGION              0x84 /* Copy region from one layer to framebuffer */
#define GPU_CMD_DRAW_TRIANGLE            0x85 /* Draw triangle - Not in original spec */
#define GPU_CMD_FILL_TRIANGLE            0x86 /* Fill triangle - Not in original spec */
#define GPU_CMD_DRAW_ELLIPSE             0x87 /* Draw ellipse - Not in original spec */
#define GPU_CMD_DRAW_BEZIER              0x88 /* Draw Bezier curve - Not in original spec */
#define GPU_CMD_DRAW_ARC                 0x89 /* Draw arc - Not in original spec */
#define GPU_CMD_DRAW_POLYGON             0x8A /* Draw polygon - Not in original spec */
#define GPU_CMD_FILL_POLYGON             0x8B /* Fill polygon - Not in original spec */
#define GPU_CMD_DRAW_TEXT                0x8C /* Draw text string - Not in original spec */

/* GPU Advanced Features (0xA0-0xCF) */
#define GPU_CMD_CONFIGURE_SHADOW_HIGHLIGHT 0xA0 /* Enable sprite shadow/highlight effects */
#define GPU_CMD_SET_LINE_INTERRUPT       0xA1 /* Trigger interrupt at specific scanline */
#define GPU_CMD_SET_PRIORITY_SORTING     0xA2 /* Set sprite priority sorting mode */
#define GPU_CMD_COPPER_LIST_START        0xA3 /* Begin "copper list" of timed commands */
#define GPU_CMD_COPPER_WAIT_LINE         0xA4 /* Wait until scanline before next command */
#define GPU_CMD_COPPER_END               0xA5 /* End copper list */
#define GPU_CMD_SET_LAYER_BLEND          0xB0 /* Set layer transparency and blend mode */
#define GPU_CMD_SET_RENDER_TARGET        0xB1 /* Set custom render target buffer - Not in original spec */
#define GPU_CMD_APPLY_SHADER             0xB2 /* Apply simple shader effect - Not in original spec */
#define GPU_CMD_CAPTURE_SCREEN           0xB3 /* Capture screen to memory buffer - Not in original spec */

/* GPU Genesis-Inspired Features (0xC0-0xCF) */
#define GPU_CMD_CONFIGURE_PLANES         0xC0 /* Configure background plane sizes (Genesis-style) */
#define GPU_CMD_SET_HSCROLL_MODE         0xC1 /* Set horizontal scroll mode */
#define GPU_CMD_SET_CELL_BASED_SPRITES   0xC2 /* Enable cell-based sprite composition */
#define GPU_CMD_SET_DUAL_PLAYFIELD       0xC3 /* Enable dual playfield mode */
#define GPU_CMD_SET_SPRITE_COLLISION_DETECTION 0xC4 /* Configure hardware sprite collision detection */

/* GPU Memory and System Commands (0xD0-0xDF) */
#define GPU_CMD_MEMORY_STATUS            0xD0 /* Get memory usage statistics - Not in original spec */
#define GPU_CMD_DUMP_VRAM                0xD1 /* Dump VRAM content for debugging - Not in original spec */
#define GPU_CMD_OPTIMIZE_MEMORY          0xD2 /* Optimize memory usage - Not in original spec */
#define GPU_CMD_RESET_PARTIAL            0xD3 /* Reset specific GPU subsystems - Not in original spec */
#define GPU_CMD_SELF_TEST                0xD4 /* Run GPU self-test routine - Not in original spec */

/*====================================================================================================================*/

/* APU System Commands (0x00-0x0F) */
#define APU_CMD_NOP                      0x00 /* No operation */
#define APU_CMD_RESET_AUDIO              0x01 /* Reset audio system to default state */
#define APU_CMD_SET_MASTER_VOLUME        0x02 /* Set master volume for all audio output */
#define APU_CMD_GET_STATUS               0x03 /* Request status packet from audio chip */
#define APU_CMD_SET_AUDIO_CONFIG         0x04 /* Configure audio output parameters */
#define APU_CMD_SYNC_TIMING              0x05 /* Synchronize timing information with CPU */
#define APU_CMD_SET_MEMORY_MODE          0x06 /* Configure memory allocation strategy */
#define APU_CMD_SET_POWER_MODE           0x07 /* Set power saving mode - Not in original spec */
#define APU_CMD_SILENCE_ALL              0x08 /* Immediately silence all audio - Not in original spec */
#define APU_CMD_AUDIO_SELF_TEST          0x09 /* Run audio hardware self-test - Not in original spec */

/* APU Tracker Commands (0x10-0x2F) */
#define APU_CMD_TRACKER_LOAD             0x10 /* Load tracker song data into specified slot */
#define APU_CMD_TRACKER_PLAY             0x11 /* Start playback of loaded tracker song */
#define APU_CMD_TRACKER_STOP             0x12 /* Stop tracker playback */
#define APU_CMD_TRACKER_PAUSE            0x13 /* Pause tracker playback */
#define APU_CMD_TRACKER_RESUME           0x14 /* Resume paused tracker playback */
#define APU_CMD_TRACKER_SET_POSITION     0x15 /* Jump to specific position in song */
#define APU_CMD_TRACKER_SET_TEMPO        0x16 /* Change playback tempo (speed) */
#define APU_CMD_TRACKER_SET_LOOP         0x17 /* Configure loop behavior */
#define APU_CMD_TRACKER_SET_CHANNEL_MASK 0x18 /* Enable/disable specific channels using bitmask */
#define APU_CMD_TRACKER_SET_PATTERN      0x19 /* Load individual pattern data */
#define APU_CMD_TRACKER_SET_INSTRUMENT   0x1A /* Load instrument definition */
#define APU_CMD_TRACKER_TRANSPOSE        0x1B /* Transpose entire song by semitones */
#define APU_CMD_TRACKER_GET_INFO         0x1C /* Get song information - Not in original spec */
#define APU_CMD_TRACKER_SET_ROW_CALLBACK 0x1D /* Set callback for specific row - Not in original spec */
#define APU_CMD_TRACKER_EXPORT           0x1E /* Export current song data - Not in original spec */
#define APU_CMD_TRACKER_IMPORT           0x1F /* Import song data from different format - Not in original spec */

/* APU Channel Control Commands (0x30-0x4F) */
#define APU_CMD_CHANNEL_SET_VOLUME       0x30 /* Set volume for specific channel */
#define APU_CMD_CHANNEL_SET_PAN          0x31 /* Set stereo panning for channel */
#define APU_CMD_CHANNEL_SET_PITCH        0x32 /* Set channel frequency/pitch directly */
#define APU_CMD_CHANNEL_NOTE_ON          0x33 /* Trigger note with velocity on channel */
#define APU_CMD_CHANNEL_NOTE_OFF         0x34 /* Stop currently playing note on channel */
#define APU_CMD_CHANNEL_SET_INSTRUMENT   0x35 /* Assign instrument to channel */
#define APU_CMD_CHANNEL_SET_EFFECT       0x36 /* Apply tracker-style effect to channel */
#define APU_CMD_CHANNEL_SET_ENVELOPE     0x37 /* Set custom ADSR envelope for channel */
#define APU_CMD_CHANNEL_PITCH_BEND       0x38 /* Apply pitch bend to channel - Not in original spec */
#define APU_CMD_CHANNEL_AFTERTOUCH       0x39 /* Apply aftertouch effect to channel - Not in original spec */
#define APU_CMD_CHANNEL_MODULATION       0x3A /* Set modulation parameters - Not in original spec */
#define APU_CMD_CHANNEL_SET_PRIORITY     0x3B /* Set channel priority for automatic voice allocation - Not in original spec */
#define APU_CMD_CHANNEL_GET_STATUS       0x3C /* Get current channel status - Not in original spec */

/* APU FM Synthesis Commands (0x50-0x6F) */
#define APU_CMD_FM_INIT_CHANNEL          0x50 /* Initialize channel for FM synthesis with algorithm */
#define APU_CMD_FM_SET_OPERATOR          0x51 /* Configure FM operator parameters */
#define APU_CMD_FM_NOTE_ON               0x52 /* Trigger FM note with velocity */
#define APU_CMD_FM_NOTE_OFF              0x53 /* Stop currently playing FM note */
#define APU_CMD_FM_SET_FEEDBACK          0x54 /* Set feedback amount for FM channel */
#define APU_CMD_FM_SET_LFO               0x55 /* Configure LFO for frequency or amplitude modulation */
#define APU_CMD_FM_LOAD_PATCH            0x56 /* Load complete FM patch preset - Not in original spec */
#define APU_CMD_FM_SAVE_PATCH            0x57 /* Save current FM settings as patch - Not in original spec */
#define APU_CMD_FM_SET_KEY_SCALING       0x58 /* Set key scaling parameters - Not in original spec */
#define APU_CMD_FM_SET_VELOCITY_SCALING  0x59 /* Set velocity scaling parameters - Not in original spec */

/* APU Sample Playback Commands (0x70-0x8F) */
#define APU_CMD_SAMPLE_LOAD              0x70 /* Load PCM sample data into memory */
#define APU_CMD_SAMPLE_PLAY              0x71 /* Play sample on specified channel */
#define APU_CMD_SAMPLE_STOP              0x72 /* Stop sample playback on channel */
#define APU_CMD_SAMPLE_LOOP_ENABLE       0x73 /* Enable/configure sample looping */
#define APU_CMD_SAMPLE_SET_POSITION      0x74 /* Set playback position within sample */
#define APU_CMD_SAMPLE_SET_PITCH         0x75 /* Adjust sample playback rate/pitch */
#define APU_CMD_SAMPLE_SET_REGION        0x76 /* Play specific portion of a sample */
#define APU_CMD_SAMPLE_REVERSE           0x77 /* Toggle reverse playback - Not in original spec */
#define APU_CMD_SAMPLE_RESAMPLE          0x78 /* Resample audio data to different rate - Not in original spec */
#define APU_CMD_SAMPLE_SET_ENDIANNESS    0x79 /* Set sample data endianness - Not in original spec */
#define APU_CMD_SAMPLE_NORMALIZE         0x7A /* Normalize sample amplitude - Not in original spec */
#define APU_CMD_SAMPLE_TRIM              0x7B /* Trim silence from sample - Not in original spec */

/* APU Wavetable Synthesis Commands (0x90-0xAF) */
#define APU_CMD_WAVE_DEFINE_TABLE        0x90 /* Define custom wavetable */
#define APU_CMD_WAVE_SET_CHANNEL         0x91 /* Assign wavetable to channel */
#define APU_CMD_WAVE_NOTE_ON             0x92 /* Play note using wavetable synthesis */
#define APU_CMD_WAVE_NOTE_OFF            0x93 /* Stop wavetable note */
#define APU_CMD_WAVE_SET_SWEEP           0x94 /* Morph between wavetables over time */
#define APU_CMD_WAVE_SET_POSITION        0x95 /* Set starting position within wavetable */
#define APU_CMD_WAVE_SET_MODULATION      0x96 /* Configure wavetable position modulation - Not in original spec */
#define APU_CMD_WAVE_SET_FORMANT         0x97 /* Apply formant filter to wavetable - Not in original spec */
#define APU_CMD_WAVE_GENERATE            0x98 /* Generate wavetable algorithmically - Not in original spec */
#define APU_CMD_WAVE_ANALYZE             0x99 /* Analyze sample to create wavetable - Not in original spec */

/* APU Effects Processing Commands (0xB0-0xCF) */
#define APU_CMD_EFFECT_SET_REVERB        0xB0 /* Configure global reverb effect */
#define APU_CMD_EFFECT_SET_DELAY         0xB1 /* Configure delay/echo effect */
#define APU_CMD_EFFECT_SET_FILTER        0xB2 /* Set filter parameters for channel (LP, HP, BP) */
#define APU_CMD_EFFECT_SET_DISTORTION    0xB3 /* Apply distortion effect to channel */
#define APU_CMD_EFFECT_CHANNEL_ROUTING   0xB4 /* Configure which effects apply to which channels */
#define APU_CMD_EFFECT_SET_EQ            0xB5 /* Configure parametric EQ for channel */
#define APU_CMD_EFFECT_SET_COMPRESSOR    0xB6 /* Configure dynamic range compressor - Not in original spec */
#define APU_CMD_EFFECT_SET_CHORUS        0xB7 /* Configure chorus effect - Not in original spec */
#define APU_CMD_EFFECT_SET_FLANGER       0xB8 /* Configure flanger effect - Not in original spec */
#define APU_CMD_EFFECT_SET_PHASER        0xB9 /* Configure phaser effect - Not in original spec */
#define APU_CMD_EFFECT_SET_BITCRUSHER    0xBA /* Configure bit crusher/decimator effect - Not in original spec */
#define APU_CMD_EFFECT_CHAIN_CONFIG      0xBB /* Configure effect processing chain - Not in original spec */

/* APU Memory Management Commands (0xD0-0xEF) */
#define APU_CMD_MEM_CLEAR_SAMPLES        0xD0 /* Clear all sample data from memory */
#define APU_CMD_MEM_CLEAR_INSTRUMENTS    0xD1 /* Clear all instrument definitions */
#define APU_CMD_MEM_CLEAR_PATTERNS       0xD2 /* Clear all pattern data */
#define APU_CMD_MEM_STATUS               0xD3 /* Request memory utilization information */
#define APU_CMD_MEM_OPTIMIZE             0xD4 /* Reorganize memory for optimal usage */
#define APU_CMD_MEM_SET_PRIORITY         0xD5 /* Set memory allocation priorities for different resource types */
#define APU_CMD_MEM_DEFRAGMENT           0xD6 /* Defragment memory - Not in original spec */
#define APU_CMD_MEM_COMPRESS             0xD7 /* Apply runtime compression to memory - Not in original spec */
#define APU_CMD_MEM_BACKUP               0xD8 /* Backup critical audio data - Not in original spec */
#define APU_CMD_MEM_RESTORE              0xD9 /* Restore audio data from backup - Not in original spec */

/*====================================================================================================================*/

/* CPU System Commands (0xE0-0xEF) */
#define CPU_CMD_SYSTEM_RESET             0xE0 /* Trigger full system reset */
#define CPU_CMD_PING                     0xE1 /* Ping/health check request */
#define CPU_CMD_GET_VERSION              0xE2 /* Get component firmware version */
#define CPU_CMD_SET_CLOCK                0xE3 /* Configure system timing */
#define CPU_CMD_SYNC                     0xE4 /* Synchronize timing between components */
#define CPU_CMD_SET_RP2350_MODE          0xE5 /* Enable RP2350-specific features */
#define CPU_CMD_PROFILE_START            0xE6 /* Start performance profiling - Not in original spec */
#define CPU_CMD_PROFILE_STOP             0xE7 /* Stop performance profiling - Not in original spec */

/* Batch Command Set (0xF0-0xF7) */
#define CMD_BATCH_SPRITES                0xF0 /* Batch multiple sprite commands */
#define CMD_BATCH_TILES                  0xF1 /* Batch multiple tile commands */
#define CMD_BATCH_LAYERS                 0xF2 /* Batch multiple layer commands */
#define CMD_BATCH_DRAW                   0xF3 /* Batch multiple drawing commands */
#define CMD_BATCH_AUDIO                  0xF4 /* Batch multiple audio commands */
#define CMD_BATCH_CHANNELS               0xF5 /* Batch multiple channel commands */
#define CMD_BATCH_CUSTOM                 0xF6 /* User-defined batch command - Not in original spec */
#define CMD_BATCH_END                    0xF7 /* End of batch sequence - Not in original spec */

/* Communication Protocol Commands (0xF8-0xFF) */
#define CMD_CLOCK_SYNC                   0xF8 /* Clock synchronization command */
#define CMD_DATA_TRANSFER                0xF9 /* Raw data transfer command */
#define CMD_ACK                          0xFA /* Command acknowledgment */
#define CMD_NAK                          0xFB /* Command not acknowledged/error */
#define CMD_READY                        0xFC /* Ready status indicator */
#define CMD_BUSY                         0xFD /* Busy status indicator */
#define CMD_ERROR                        0xFE /* Error status with code */
#define CMD_EXTENDED                     0xFF /* Extended command set (followed by 16-bit command ID) */

/*====================================================================================================================*/


#endif /* TRIBOY_COMMANDS_H */