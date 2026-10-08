#include "pressure_adc.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
bool fake_adc_eoc;
static bool start_fail, stop_fail;
static unsigned starts, stops;
static uint32_t sample;
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h)
{ (void)h; ++starts; fake_adc_eoc = false; return start_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h)
{ (void)h; ++stops; return stop_fail ? HAL_ERROR : HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { (void)h; return sample; }
int main(void)
{
  ADC_HandleTypeDef adc = {0};
  uint16_t raw = 999;
  PressureAdc_Init(&adc);
  CHECK(!PressureAdc_Read(&raw));
  PressureAdc_Poll(0); PressureAdc_Poll(9);
  CHECK(starts == 1 && stops == 0);
  PressureAdc_Poll(10);
  CHECK(stops == 1 && !PressureAdc_Read(&raw));
  for (unsigned i = 0; i < 3; ++i)
  {
    sample = i == 0 ? 0 : i == 1 ? 2048 : 4095;
    PressureAdc_Poll(20 + i * 2); fake_adc_eoc = true;
    PressureAdc_Poll(21 + i * 2);
    CHECK(PressureAdc_Read(&raw) && raw == sample);
  }
  CHECK(!PressureAdc_Read(NULL));
  PressureAdc_Poll(UINT32_MAX - 4); PressureAdc_Poll(5);
  CHECK(!PressureAdc_Read(&raw));
  start_fail = true; PressureAdc_Poll(10);
  CHECK(!PressureAdc_Read(&raw));
  start_fail = false; stop_fail = true;
  PressureAdc_Poll(11); fake_adc_eoc = true; PressureAdc_Poll(12);
  CHECK(!PressureAdc_Read(&raw));
  stop_fail = false; sample = 4096;
  PressureAdc_Poll(13); fake_adc_eoc = true; PressureAdc_Poll(14);
  CHECK(!PressureAdc_Read(&raw));
  PressureAdc_Init(NULL); PressureAdc_Poll(20);
  CHECK(!PressureAdc_Read(&raw));
  return 0;
}
