#include "pressure_adc.h"

static ADC_HandleTypeDef *handle;
static uint32_t started;
static uint16_t value;
static bool pending;
static bool valid;

void PressureAdc_Init(ADC_HandleTypeDef *adc)
{
  handle = adc;
  pending = false;
  valid = false;
}

void PressureAdc_Poll(uint32_t now)
{
  if (!handle) return;
  if (!pending)
  {
    if (HAL_ADC_Start(handle) == HAL_OK)
    {
      pending = true;
      started = now;
    }
    else valid = false;
    return;
  }
  if (__HAL_ADC_GET_FLAG(handle, ADC_FLAG_EOC))
  {
    value = (uint16_t)HAL_ADC_GetValue(handle);
    valid = HAL_ADC_Stop(handle) == HAL_OK && value <= 4095;
    pending = false;
  }
  else if ((uint32_t)(now - started) >= 10U)
  {
    (void)HAL_ADC_Stop(handle);
    pending = false;
    valid = false;
  }
}

bool PressureAdc_Read(uint16_t *raw)
{
  if (!valid || !raw) return false;
  *raw = value;
  return true;
}
