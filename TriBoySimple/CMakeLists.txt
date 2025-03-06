cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(TriBoySimple C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add common include directories
include_directories(${CMAKE_CURRENT_LIST_DIR})

# Add debug screen library
add_library(lcd_display STATIC
    lcd_display.c
)

target_link_libraries(lcd_display
    pico_stdlib
    hardware_i2c
)

# Define the three executables
add_executable(TriBoySimpleCPU simple_cpu.c)
add_executable(TriBoySimpleAPU simple_apu.c)
add_executable(TriBoySimpleGPU simple_gpu.c)

# Set program properties for each executable
foreach(TARGET TriBoySimpleCPU TriBoySimpleAPU TriBoySimpleGPU)
    pico_set_program_name(${TARGET} ${TARGET})
    pico_set_program_version(${TARGET} "0.1")

    # Enable/disable UART/USB output
    pico_enable_stdio_uart(${TARGET} 1)
    pico_enable_stdio_usb(${TARGET} 0)

    # Add the standard include files to the build
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/simple_example
    )

    # Add shared libraries for all targets
    target_link_libraries(${TARGET}
        pico_stdlib
        hardware_spi
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_timer
        hardware_watchdog
        hardware_clocks
        lcd_display
    )

    # Generate additional output files (.uf2, etc.)
    pico_add_extra_outputs(${TARGET})
endforeach()

# Add multicore library to CPU and GPU which use it
target_link_libraries(TriBoySimpleCPU pico_multicore)
target_link_libraries(TriBoySimpleGPU pico_multicore)
target_link_libraries(TriBoySimpleAPU pico_multicore)