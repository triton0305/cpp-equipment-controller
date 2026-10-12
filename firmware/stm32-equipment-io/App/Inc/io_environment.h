#ifndef IO_ENVIRONMENT_H
#define IO_ENVIRONMENT_H
#include "io_protocol.h"
#include "stm32f4xx_hal.h"
#define IO_ENVIRONMENT_TIMEOUT_MS 5000U
/* Foreground only. Init after TIM5 configuration; Reset disables the adapter. */
void IoEnvironment_Reset(void);
void IoEnvironment_Init(TIM_HandleTypeDef *timer);
void IoEnvironment_Poll(void);
/* Consumes a new result once; never repeatedly sends cached numeric readings. */
bool IoEnvironment_Valid(uint32_t now);
void IoEnvironment_Take(IoSensors *sensors, uint32_t now);
#endif
