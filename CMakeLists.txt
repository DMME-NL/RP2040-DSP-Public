cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

project(Main C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable and source files
add_executable(Main
    Main.c
    lib/i2s/i2s.c
    lib/ssd1306/ssd1306.c
    lib/ssd1306/font.c
    lib/spi_ram/spi_ram.h
)

# Generate PIO header for the i2s PIO program and associate it with Main target
pico_generate_pio_header(Main ${CMAKE_CURRENT_LIST_DIR}/lib/i2s/i2s.pio)

# Include directories for headers (also include build dir for generated headers)
target_include_directories(Main PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/lib
    ${CMAKE_CURRENT_LIST_DIR}/lib/i2s
    ${CMAKE_CURRENT_LIST_DIR}/lib/ssd1306
    ${CMAKE_CURRENT_LIST_DIR}/lib/spi_ram    
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/ui
    ${CMAKE_CURRENT_LIST_DIR}/src/flash
    ${CMAKE_CURRENT_LIST_DIR}/src/effects
    ${CMAKE_CURRENT_BINARY_DIR}  # for i2s.pio.h generated header
)

# Link necessary libraries
target_link_libraries(Main
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_gpio
    hardware_i2c
    hardware_adc
    hardware_spi
)

# Enable USB stdio and disable UART stdio
pico_enable_stdio_uart(Main 0)
pico_enable_stdio_usb(Main 1)

# Generate UF2, ELF, etc outputs
pico_add_extra_outputs(Main)
