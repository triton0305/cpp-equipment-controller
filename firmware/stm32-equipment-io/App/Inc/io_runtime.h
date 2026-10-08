#ifndef IO_RUNTIME_H
#define IO_RUNTIME_H
#include "stm32f4xx_hal.h"
/* Single foreground owner; call Poll continuously, never from an ISR. */
void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart);
void IoRuntime_Poll(void);
/* Debugger-visible diagnostics, no new wire messages. */
extern uint32_t io_runtime_rx_errors;
extern uint32_t io_runtime_unsupported_valve_commands;
#endif
