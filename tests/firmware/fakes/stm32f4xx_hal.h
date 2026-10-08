#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
#include <stdbool.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { unsigned unused; } ADC_HandleTypeDef;
#define ADC_FLAG_EOC 1U
extern bool fake_adc_eoc;
#define __HAL_ADC_GET_FLAG(h, f) ((void)(h), (void)(f), fake_adc_eoc)
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h);
#endif
