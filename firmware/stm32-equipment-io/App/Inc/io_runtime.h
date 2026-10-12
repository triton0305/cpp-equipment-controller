#ifndef IO_RUNTIME_H
#define IO_RUNTIME_H
#include "stm32f4xx_hal.h"
#include <stdbool.h>
/* Provisional host-verified limits; tune only after hardware timing verification. */
#define IO_CONTROL_TIMEOUT_MS 1000U
#define IO_CONTROL_REFRESH_WINDOW_MS 250U
/* Single foreground owner; call Poll continuously, never from an ISR. */
void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart);
void IoRuntime_Poll(void);
/* Debugger-visible diagnostics, no new wire messages. */
extern bool io_runtime_control_valid;
extern bool io_runtime_failsafe_latched;
extern uint32_t io_runtime_rx_errors;
extern uint32_t io_runtime_unsupported_valve_commands;
#endif
