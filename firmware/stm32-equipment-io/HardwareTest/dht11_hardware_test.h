#ifndef DHT11_HARDWARE_TEST_H
#define DHT11_HARDWARE_TEST_H

#include "stm32f4xx_hal.h"

/* Isolated bench harness: do not run IoRuntime or the Linux controller with it. */
void Dht11HardwareTest_Init(TIM_HandleTypeDef *timer, UART_HandleTypeDef *uart);
void Dht11HardwareTest_Poll(void);

#endif
