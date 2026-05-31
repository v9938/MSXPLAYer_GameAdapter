# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD weact_studio_rp2350b_core CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(MSXPLAYer_CR VERSION 1.40 LANGUAGES C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

cmake_minimum_required(VERSION 3.13)
string(TIMESTAMP BUILD_DATE "%Y%m%d%H%M")



add_executable(MSXPLAYer_CR)

# Create a directory for generated files
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

# Generate the PIO header file from ws2812.pio
pico_generate_pio_header(MSXPLAYer_CR ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)
pico_generate_pio_header(MSXPLAYer_CR ${CMAKE_CURRENT_LIST_DIR}/pwm_low_hiz.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

target_sources(MSXPLAYer_CR PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/commands.c
        ${CMAKE_CURRENT_LIST_DIR}/ports.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(MSXPLAYer_CR PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/WS2812/include
        )

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example
target_link_libraries(MSXPLAYer_CR 
    PUBLIC pico_stdlib 
    pico_unique_id 
    tinyusb_device 
    tinyusb_board 
    hardware_pwm
    hardware_clocks
    hardware_pio
    hardware_gpio 
    pico_multicore)

# pico_enable_stdio_usb(MSXPLAYer_CR 1)
pico_enable_stdio_uart(MSXPLAYer_CR 1)
pico_add_extra_outputs(MSXPLAYer_CR)

# add url via pico_set_program_url
target_link_options(${PROJECT_NAME} PRIVATE -Wl,-Map=${PROJECT_NAME}.map)
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--print-memory-usage)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
  PICO_DEFAULT_UART=0 # UART0なら0, UART1なら1
  PICO_DEFAULT_UART_TX_PIN=0 # TXピンのGP番号
  PICO_DEFAULT_UART_RX_PIN=1 # RXピンのGP番号
  PROJECT_VERSION="${BUILD_DATE}"

)