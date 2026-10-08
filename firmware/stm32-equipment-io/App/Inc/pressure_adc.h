#ifndef PRESSURE_ADC_H
#define PRESSURE_ADC_H
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"
/* Cooperative single conversions. No ADC configuration changes. */
void PressureAdc_Init(ADC_HandleTypeDef *adc);
void PressureAdc_Poll(uint32_t now);
bool PressureAdc_Read(uint16_t *raw);
#endif
