# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(PICO_DEOPTIMIZED_DEBUG on)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
#set(PICO_SDK_PATH "~/pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picotest C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(picotest picotest.cpp)

pico_set_program_name(picotest "picotest")
pico_set_program_version(picotest "0.1")

pico_enable_stdio_uart(picotest 1)
pico_enable_stdio_usb(picotest 0)

# Add the standard library to the build
target_link_libraries(picotest pico_stdlib pico_multicore)

# Add any user requested libraries
target_link_libraries(picotest
        hardware_dma
        hardware_pio
        hardware_interp
        hardware_timer
        hardware_clocks
        hardware_pwm
        dvi
)

target_include_directories(picotest
PRIVATE
        pico_lib
)


pico_add_extra_outputs(picotest)

add_subdirectory(pico_lib/dvi)



