#ifndef UART_IO_H
#define UART_IO_H
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
void UartIo_Init(UART_HandleTypeDef *uart);
/* 1 = byte, 0 = none, -1 = receive error (partial line must be discarded). */
int UartIo_TryRead(uint8_t *byte);
bool UartIo_TryWrite(uint8_t byte);
#endif
