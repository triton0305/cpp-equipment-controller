cmake_minimum_required(VERSION 3.16)

project(cpp_equipment_controller
  VERSION 1.0.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(equipment_controller
  src/main.cpp
  src/equipment_controller.cpp
  src/logger.cpp
  src/ascii_protocol.cpp
)

target_include_directories(equipment_controller PRIVATE
  include
)

target_compile_options(equipment_controller PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)