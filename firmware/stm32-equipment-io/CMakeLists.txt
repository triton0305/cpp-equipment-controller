cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32-equipment-io)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
option(STEPPER_HARDWARE_TEST "Build isolated command-started stepper bench firmware" OFF)
option(DHT11_HARDWARE_TEST "Build isolated DHT11 hardware verification harness" OFF)
if(STEPPER_HARDWARE_TEST AND DHT11_HARDWARE_TEST)
    message(FATAL_ERROR "Select only one isolated hardware test firmware")
endif()
set(IO_RUNTIME_SOURCE App/Src/io_runtime.c)
if(STEPPER_HARDWARE_TEST)
    # Preserve main.c and all CubeMX configuration. Substitute only the runtime
    # entry adapters in this explicit bench build; production uses the real file.
    set(IO_RUNTIME_SOURCE HardwareTest/stepper_hardware_test.c)
endif()
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/Src/dht11.c
    App/Src/dht11_frame.c
    App/Src/device_io.c
    App/Src/pressure_adc.c
    App/Src/io_protocol.c
    App/Src/uart_io.c
    App/Src/stepper.c
    ${IO_RUNTIME_SOURCE}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Opt-in bench firmware; normal Debug/Release builds retain the production runtime.
if(DHT11_HARDWARE_TEST)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        HardwareTest/dht11_hardware_test.c
        HardwareTest/dht11_diagnostic.c)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE HardwareTest)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DHT11_HARDWARE_TEST=1)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/dht11-hardware-test.bin
        COMMENT "Generate isolated DHT11 harness binary (not production firmware)"
        VERBATIM)
endif()

if(STEPPER_HARDWARE_TEST)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/stepper-hardware-test.bin
        COMMENT "Generate isolated stepper bench binary (not production firmware)"
        VERBATIM)
endif()
