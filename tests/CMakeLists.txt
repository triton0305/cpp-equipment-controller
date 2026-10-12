add_executable(verify_scenario verify_scenario.cpp)
add_executable(test_controller core/test_controller.cpp)
target_link_libraries(test_controller PRIVATE equipment_core)
add_executable(test_ascii_protocol protocol/test_ascii_protocol.cpp)
target_link_libraries(test_ascii_protocol PRIVATE equipment_protocol)
add_executable(test_serial_transport transport/test_serial_transport.cpp)
target_link_libraries(test_serial_transport PRIVATE equipment_transport)
foreach(n RANGE 1 26)
  if(n LESS 16)
    set(test_binary test_controller)
  elseif(n LESS 20)
    set(test_binary test_ascii_protocol)
  else()
    set(test_binary test_serial_transport)
  endif()
  add_test(NAME TEST_${n} COMMAND $<TARGET_FILE:verify_scenario> ${n} $<TARGET_FILE:${test_binary}> ${CMAKE_CURRENT_SOURCE_DIR}/baseline_output.txt)
  set_tests_properties(TEST_${n} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()

add_executable(test_runtime_uart transport/test_runtime_uart.cpp)
target_link_libraries(test_runtime_uart PRIVATE util)
add_test(NAME TEST_27 COMMAND test_runtime_uart $<TARGET_FILE:equipment_controller>)

add_executable(test_runtime_lifecycle transport/test_runtime_lifecycle.cpp)
target_link_libraries(test_runtime_lifecycle PRIVATE util)
add_test(NAME TEST_28 COMMAND test_runtime_lifecycle $<TARGET_FILE:equipment_controller>)
set_tests_properties(TEST_28 PROPERTIES TIMEOUT 20)

add_executable(test_file_logging logging/test_file_logging.cpp)
target_link_libraries(test_file_logging PRIVATE equipment_core)
add_test(NAME TEST_29 COMMAND test_file_logging)
set_tests_properties(TEST_29 PROPERTIES TIMEOUT 5)

add_executable(test_safe_valve core/test_safe_valve.cpp)
target_link_libraries(test_safe_valve PRIVATE equipment_core)
add_test(NAME TEST_30 COMMAND test_safe_valve)
set_tests_properties(TEST_30 PROPERTIES TIMEOUT 5)

# Compile the actual firmware C helpers on the host with a small HAL fake.
enable_language(C)
set(FIRMWARE_APP ${PROJECT_SOURCE_DIR}/firmware/stm32-equipment-io/App)
add_executable(test_pressure_adc firmware/test_pressure_adc.c ${FIRMWARE_APP}/Src/pressure_adc.c)
target_include_directories(test_pressure_adc PRIVATE firmware/fakes ${FIRMWARE_APP}/Inc)
add_test(NAME STM32_ADC_HOST COMMAND test_pressure_adc)

add_library(stm32_io_protocol STATIC ${FIRMWARE_APP}/Src/io_protocol.c)
target_include_directories(stm32_io_protocol PUBLIC ${FIRMWARE_APP}/Inc)
add_executable(test_io_protocol firmware/test_io_protocol.cpp)
target_link_libraries(test_io_protocol PRIVATE stm32_io_protocol equipment_protocol)
add_test(NAME STM32_PROTOCOL_CONTRACT COMMAND test_io_protocol)

add_executable(test_io_runtime firmware/test_io_runtime.cpp
  ${FIRMWARE_APP}/Src/io_runtime.c ${FIRMWARE_APP}/Src/device_io.c ${FIRMWARE_APP}/Src/pressure_adc.c)
target_include_directories(test_io_runtime PRIVATE firmware/fakes ${FIRMWARE_APP}/../Core/Inc)
target_link_libraries(test_io_runtime PRIVATE stm32_io_protocol equipment_protocol)
add_test(NAME STM32_RUNTIME_HOST COMMAND test_io_runtime)

add_executable(test_uart_io firmware/test_uart_io.c ${FIRMWARE_APP}/Src/uart_io.c)
target_include_directories(test_uart_io PRIVATE firmware/fakes ${FIRMWARE_APP}/Inc)
add_test(NAME STM32_UART_ADAPTER_HOST COMMAND test_uart_io)

add_executable(test_dht11_frame firmware/test_dht11_frame.c ${FIRMWARE_APP}/Src/dht11_frame.c)
target_include_directories(test_dht11_frame PRIVATE ${FIRMWARE_APP}/Inc)
add_test(NAME STM32_DHT11_DECODE_HOST COMMAND test_dht11_frame)

add_executable(test_dht11 firmware/test_dht11.c
  ${FIRMWARE_APP}/Src/dht11.c ${FIRMWARE_APP}/Src/dht11_frame.c)
target_include_directories(test_dht11 PRIVATE firmware/dht11_fakes
  ${FIRMWARE_APP}/Inc ${FIRMWARE_APP}/../Core/Inc)
add_test(NAME STM32_DHT11_DRIVER_HOST COMMAND test_dht11)
set_tests_properties(STM32_DHT11_DECODE_HOST STM32_DHT11_DRIVER_HOST PROPERTIES TIMEOUT 5)

# Exercise bench diagnostics separately; preserve the normal driver test target.
add_executable(test_dht11_diagnostic firmware/test_dht11.c
  ${FIRMWARE_APP}/Src/dht11.c ${FIRMWARE_APP}/Src/dht11_frame.c
  ${FIRMWARE_APP}/../HardwareTest/dht11_diagnostic.c
  ${FIRMWARE_APP}/../HardwareTest/dht11_hardware_test.c)
target_compile_definitions(test_dht11_diagnostic PRIVATE DHT11_HARDWARE_TEST=1)
target_include_directories(test_dht11_diagnostic PRIVATE firmware/dht11_fakes
  ${FIRMWARE_APP}/Inc ${FIRMWARE_APP}/../Core/Inc ${FIRMWARE_APP}/../HardwareTest)
add_test(NAME STM32_DHT11_DIAGNOSTIC_HOST COMMAND test_dht11_diagnostic)
set_tests_properties(STM32_DHT11_DIAGNOSTIC_HOST PROPERTIES TIMEOUT 5)
