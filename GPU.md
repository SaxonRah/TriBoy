# GPU Command Set Architecture for TriBoy System
This command set provides a comprehensive foundation for a 16-bit style GPU that can deliver graphics comparable to the Sega Genesis while leveraging the capabilities of the RP2040/RP2350 architecture for modern implementations.

## GPU Command Structure

Each command will follow this format:
- 1 byte: Command ID
- 1 byte: Command length (including Command ID and length bytes)
- N bytes: Command parameters

## Core Features & Memory Allocation

### RP2040 (264KB SRAM)
- Framebuffer: 128KB (320×240×8-bit or 256×240×16-bit)
- Tile cache: 64KB
- Sprite data: 48KB
- Command buffer: 16KB
- Work RAM: 8KB

### RP2350 (520KB SRAM)
- Framebuffer: 240KB (320×240×16-bit or 400×240×16-bit)
- Tile cache: 128KB
- Sprite data: 96KB
- Command buffer: 32KB
- Work RAM: 24KB

## Command Set

### System Commands (0x00-0x0F)

```
0x00: NOP
Length: 2
Parameters: None
Description: No operation

0x01: RESET_GPU
Length: 2
Parameters: None
Description: Reset GPU state to defaults

0x02: SET_DISPLAY_MODE
Length: 5
Parameters: 
  - Width (2 bytes)
  - Height (2 bytes)
  - BPP (1 byte): 4, 8, or 16
Description: Configure display resolution and color depth

0x03: SET_VBLANK_CALLBACK
Length: 3
Parameters:
  - Enable (1 byte): 0=disable, 1=enable
Description: Toggle VBLANK interrupt signal to CPU

0x04: VSYNC_WAIT
Length: 2
Parameters: None
Description: Notify CPU when next VBLANK occurs

0x05: GET_STATUS
Length: 2
Parameters: None
Description: Return GPU status flags
```

### Palette Commands (0x10-0x1F)

```
0x10: SET_PALETTE_ENTRY
Length: 5
Parameters:
  - Index (1 byte)
  - R (1 byte)
  - G (1 byte)
  - B (1 byte)
Description: Set single palette entry

0x11: LOAD_PALETTE
Length: Variable
Parameters:
  - Start index (1 byte)
  - Count (1 byte)
  - Color data (Count * 3 bytes)
Description: Load multiple palette entries

0x12: SET_TRANSPARENT_COLOR
Length: 3
Parameters:
  - Color index (1 byte)
Description: Set transparent color index for sprites
```

### Background Layer Commands (0x20-0x3F)

```
0x20: CONFIGURE_LAYER
Length: 9
Parameters:
  - Layer ID (1 byte): 0-3
  - Enable (1 byte): 0=disable, 1=enable
  - Priority (1 byte): 0=highest, 3=lowest
  - Scroll mode (1 byte): 0=none, 1=full, 2=column, 3=line
  - Tile width (1 byte): 8 or 16
  - Tile height (1 byte): 8 or 16
  - Width in tiles (1 byte)
  - Height in tiles (1 byte)
Description: Configure background layer properties

0x21: LOAD_TILESET
Length: Variable
Parameters:
  - Layer ID (1 byte)
  - Tile start index (2 bytes)
  - Tile count (2 bytes)
  - Compression (1 byte): 0=none, 1=RLE
  - Tile data (variable)
Description: Load tile graphics data

0x22: LOAD_TILEMAP
Length: Variable
Parameters:
  - Layer ID (1 byte)
  - X position (1 byte)
  - Y position (1 byte)
  - Width (1 byte)
  - Height (1 byte)
  - Compression (1 byte): 0=none, 1=RLE
  - Tilemap data (variable)
Description: Load tile mapping data

0x23: SCROLL_LAYER
Length: 6
Parameters:
  - Layer ID (1 byte)
  - X scroll (2 bytes)
  - Y scroll (2 bytes)
Description: Set layer scroll position

0x24: SET_HSCROLL_TABLE
Length: Variable
Parameters:
  - Layer ID (1 byte)
  - Start line (1 byte)
  - Line count (1 byte)
  - Scroll values (2 bytes * Line count)
Description: Set per-line horizontal scroll values

0x25: SET_VSCROLL_TABLE
Length: Variable
Parameters:
  - Layer ID (1 byte)
  - Start column (1 byte)
  - Column count (1 byte)
  - Scroll values (2 bytes * Column count)
Description: Set per-column vertical scroll values
```

### Sprite Commands (0x40-0x5F)

```
0x40: LOAD_SPRITE_PATTERN
Length: Variable
Parameters:
  - Pattern ID (1 byte)
  - Width (1 byte): In 8-pixel units
  - Height (1 byte): In 8-pixel units
  - BPP (1 byte): 4, 8, or 16
  - Compression (1 byte): 0=none, 1=RLE
  - Pattern data (variable)
Description: Load sprite pattern/graphic data

0x41: DEFINE_SPRITE
Length: 10
Parameters:
  - Sprite ID (1 byte)
  - Pattern ID (1 byte)
  - X position (2 bytes): Fixed-point position
  - Y position (2 bytes): Fixed-point position
  - Attributes (1 byte): Bit flags for flip/priority
  - Palette offset (1 byte): For 4/8-bit sprites
  - Scale (1 byte): 0-255 representing 0.0-2.0
Description: Define sprite properties

0x42: MOVE_SPRITE
Length: 6
Parameters:
  - Sprite ID (1 byte)
  - X position (2 bytes)
  - Y position (2 bytes)
Description: Update sprite position

0x43: SET_SPRITE_ATTRIBUTES
Length: 4
Parameters:
  - Sprite ID (1 byte)
  - Attributes (1 byte): Bit flags
  - Palette offset (1 byte)
Description: Update sprite attributes

0x44: HIDE_SPRITE
Length: 3
Parameters:
  - Sprite ID (1 byte)
Description: Hide a sprite

0x45: SHOW_SPRITE
Length: 3
Parameters:
  - Sprite ID (1 byte)
Description: Show a previously hidden sprite

0x46: ANIMATE_SPRITE
Length: 6
Parameters:
  - Sprite ID (1 byte)
  - Start frame (1 byte)
  - End frame (1 byte)
  - Frame rate (1 byte): Frames per second
  - Loop mode (1 byte): 0=once, 1=loop, 2=ping-pong
Description: Set up sprite animation
```

### Special Effects Commands (0x60-0x7F)

```
0x60: SET_FADE
Length: 4
Parameters:
  - Mode (1 byte): 0=fade in, 1=fade out
  - Level (1 byte): 0-255
Description: Set screen fade level

0x61: MOSAIC_EFFECT
Length: 3
Parameters:
  - Size (1 byte): 0=off, 1-15=size
Description: Apply mosaic pixelation effect

0x62: SCANLINE_EFFECT
Length: Variable
Parameters:
  - Mode (1 byte): 0=brightness, 1=color adjust
  - Line count (1 byte)
  - Effect values (variable)
Description: Apply per-scanline effects

0x63: ROTATION_ZOOM_BACKGROUND
Length: 8
Parameters:
  - Layer ID (1 byte)
  - Angle (2 bytes): 0-1023 (0-359.9°)
  - Scale X (2 bytes): Fixed-point value
  - Scale Y (2 bytes): Fixed-point value
Description: Apply affine transformation to background

0x64: SET_WINDOW
Length: 7
Parameters:
  - Window ID (1 byte): 0 or 1
  - X1 (1 byte)
  - Y1 (1 byte)
  - X2 (1 byte)
  - Y2 (1 byte)
  - Layer mask (1 byte): Bit flags for affected layers
Description: Define rectangular window for clipping

0x65: COLOR_MATH
Length: 3
Parameters:
  - Mode (1 byte): 0=add, 1=subtract, 2=average
Description: Set blending mode between layers
```

### Direct Drawing Commands (0x80-0x9F)

```
0x80: DRAW_PIXEL
Length: 6
Parameters:
  - X (2 bytes)
  - Y (2 bytes)
  - Color (1 byte)
Description: Draw single pixel

0x81: DRAW_LINE
Length: 8
Parameters:
  - X1 (2 bytes)
  - Y1 (2 bytes)
  - X2 (2 bytes)
  - Y2 (2 bytes)
  - Color (1 byte)
Description: Draw line between two points

0x82: DRAW_RECT
Length: 9
Parameters:
  - X (2 bytes)
  - Y (2 bytes)
  - Width (2 bytes)
  - Height (2 bytes)
  - Color (1 byte)
  - Fill (1 byte): 0=outline, 1=filled
Description: Draw rectangle

0x83: DRAW_CIRCLE
Length: 8
Parameters:
  - X (2 bytes)
  - Y (2 bytes)
  - Radius (2 bytes)
  - Color (1 byte)
  - Fill (1 byte): 0=outline, 1=filled
Description: Draw circle

0x84: BLIT_REGION
Length: 11
Parameters:
  - Src layer (1 byte)
  - Src X (2 bytes)
  - Src Y (2 bytes)
  - Width (2 bytes)
  - Height (2 bytes)
  - Dest X (2 bytes)
  - Dest Y (2 bytes)
Description: Copy region from one layer to framebuffer
```

### Advanced Features (0xA0-0xCF)

```
0xA0: CONFIGURE_SHADOW_HIGHLIGHT
Length: 4
Parameters:
  - Enable (1 byte): 0=disable, 1=enable
  - Shadow intensity (1 byte): 0-255
  - Highlight intensity (1 byte): 0-255
Description: Enable sprite shadow/highlight effects

0xA1: SET_LINE_INTERRUPT
Length: 4
Parameters:
  - Line number (2 bytes)
  - Enable (1 byte): 0=disable, 1=enable
Description: Trigger interrupt at specific scanline

0xA2: SET_PRIORITY_SORTING
Length: 3
Parameters:
  - Mode (1 byte): 0=Y-position, 1=manual priority
Description: Set sprite priority sorting mode

0xA3: COPPER_LIST_START
Length: 3
Parameters:
  - Size (1 byte): Number of commands to follow
Description: Begin "copper list" of timed commands

0xA4: COPPER_WAIT_LINE
Length: 3
Parameters:
  - Line (1 byte)
Description: Wait until scanline before next command

0xA5: COPPER_END
Length: 2
Parameters: None
Description: End copper list

0xB0: SET_LAYER_BLEND
Length: 4
Parameters:
  - Layer ID (1 byte)
  - Alpha (1 byte): 0-255
  - Blend mode (1 byte): 0=normal, 1=add, 2=multiply
Description: Set layer transparency and blend mode
```

## Sega Genesis-Inspired Features

```
0xC0: CONFIGURE_PLANES
Length: 3
Parameters:
  - Plane A size (1 byte): 0=32×32, 1=64×32, 2=32×64, 3=64×64
  - Plane B size (1 byte): 0=32×32, 1=64×32, 2=32×64, 3=64×64
Description: Configure background plane sizes (Genesis-style)

0xC1: SET_HSCROLL_MODE
Length: 3
Parameters:
  - Mode (1 byte): 0=whole, 1=tile, 2=line
Description: Set horizontal scroll mode

0xC2: SET_CELL_BASED_SPRITES
Length: 4
Parameters:
  - Enable (1 byte): 0=disable, 1=enable
  - Cell width (1 byte): typically 8
  - Cell height (1 byte): typically 8 or 16
Description: Enable cell-based sprite composition

0xC3: SET_DUAL_PLAYFIELD
Length: 3
Parameters:
  - Enable (1 byte): 0=disable, 1=enable
Description: Enable dual playfield mode

0xC4: SET_SPRITE_COLLISION_DETECTION
Length: 3
Parameters:
  - Mode (1 byte): 0=off, 1=sprite-sprite, 2=sprite-BG, 3=both
Description: Configure hardware sprite collision detection
```

## Implementation Notes

1. **Processing Optimization**:
   - Core 0 handles command processing, background rendering
   - Core 1 manages sprites, scanline effects and final compositing
   - Use DMA for transferring data to/from display

2. **Layer Architecture**:
   - 4 background layers (2 main + 1 window + 1 foreground)
   - Sprite layer with 64-128 hardware sprites
   - Each layer has independent transparency and effects

3. **RP2350 Enhanced Features**:
   - Higher resolution modes
   - 16-bit color throughout the pipeline
   - More simultaneous sprites (128 vs 64)
   - Enhanced rotation/scaling for backgrounds
   - Hardware-accelerated layer blending

4. **Memory Management**:
   - Implement dirty rectangle tracking
   - Tile caching with least-recently-used replacement
   - Sprite attribute tables similar to OAM in commercial consoles
