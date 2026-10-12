#include "dht11.h"
#include "main.h"
#include <stdbool.h>

static TIM_HandleTypeDef *clock_timer;
static uint32_t last_attempt_ms, low_started_ms, capture_started_us, capture_started_ms;
static uint32_t ready_delay_ms;
static bool pending;

static void input_mode(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DHT11_DATA_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &gpio);
}

static uint32_t micros(void)
{
  return __HAL_TIM_GET_COUNTER(clock_timer);
}

/* The tick deadline also prevents an endless wait if TIM5 stops advancing.
 * HAL tick and TIM5 must be running; never call from an ISR/IRQ-masked context. */
static bool wait_level(GPIO_PinState level, uint32_t timeout_us)
{
  const uint32_t start = micros();
  for (;;)
  {
    const uint32_t now = micros();
    if ((uint32_t)(now - capture_started_us) >= DHT11_CAPTURE_LIMIT_US ||
        (uint32_t)(HAL_GetTick() - capture_started_ms) >= 10U ||
        (uint32_t)(now - start) >= timeout_us)
      return false;
    if (HAL_GPIO_ReadPin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin) == level)
      return true;
  }
}

static Dht11Result capture(Dht11Sample *sample)
{
  uint8_t frame[5] = {0};
  capture_started_us = micros();
  capture_started_ms = HAL_GetTick();
  input_mode(); /* Release, never actively drive HIGH against the sensor. */
  if (!wait_level(GPIO_PIN_SET, 20U) || !wait_level(GPIO_PIN_RESET, 100U) ||
      !wait_level(GPIO_PIN_SET, 120U) || !wait_level(GPIO_PIN_RESET, 120U))
    return DHT11_RESPONSE_TIMEOUT;

  for (unsigned bit = 0; bit < 40U; ++bit)
  {
    if (!wait_level(GPIO_PIN_SET, 100U)) return DHT11_DATA_TIMEOUT;
    const uint32_t rising = micros();
    if (!wait_level(GPIO_PIN_RESET, 100U)) return DHT11_DATA_TIMEOUT;
    const uint32_t width = (uint32_t)(micros() - rising);
    /* Reject ambiguous/noisy highs instead of treating every pulse as a bit. */
    if (!((width >= 15U && width <= 40U) || (width >= 55U && width <= 90U)))
      return DHT11_INVALID_DATA;
    frame[bit / 8U] = (uint8_t)((frame[bit / 8U] << 1U) | (width >= 55U));
  }
  if (!wait_level(GPIO_PIN_SET, 100U)) return DHT11_DATA_TIMEOUT;
  return Dht11_Decode(frame, sizeof(frame), sample);
}

Dht11Result Dht11_Init(TIM_HandleTypeDef *timer)
{
  if (pending) input_mode();
  pending = false;
  clock_timer = NULL;
  if (!timer || timer->Instance != TIM5 || timer->Init.Prescaler != 83U ||
      timer->Init.Period != UINT32_MAX || timer->Init.CounterMode != TIM_COUNTERMODE_UP)
    return DHT11_INVALID_DATA;
  if (HAL_TIM_Base_Start(timer) != HAL_OK) return DHT11_HAL_ERROR;
  clock_timer = timer;
  input_mode();
  last_attempt_ms = HAL_GetTick();
  ready_delay_ms = DHT11_STABILIZE_MS;
  return DHT11_OK;
}

Dht11Result Dht11_Request(void)
{
  if (!clock_timer) return DHT11_NOT_READY;
  if (pending) return DHT11_BUSY;
  const uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - last_attempt_ms) < ready_delay_ms) return DHT11_NOT_READY;
  GPIO_InitTypeDef gpio = {0};
  HAL_GPIO_WritePin(DHT11_DATA_GPIO_Port, DHT11_DATA_Pin, GPIO_PIN_RESET);
  gpio.Pin = DHT11_DATA_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(DHT11_DATA_GPIO_Port, &gpio);
  low_started_ms = HAL_GetTick();
  last_attempt_ms = low_started_ms;
  ready_delay_ms = DHT11_INTERVAL_MS;
  pending = true;
  return DHT11_BUSY;
}

Dht11Result Dht11_Poll(Dht11Sample *sample)
{
  if (!sample)
  {
    Dht11_Cancel();
    return DHT11_INVALID_DATA;
  }
  if (!pending) return DHT11_NOT_READY;
  const uint32_t elapsed = (uint32_t)(HAL_GetTick() - low_started_ms);
  if (elapsed < DHT11_START_LOW_MS) return DHT11_BUSY;
  pending = false;
  if (elapsed >= 30U)
  {
    input_mode();
    return DHT11_DATA_TIMEOUT;
  }
  return capture(sample);
}

void Dht11_Cancel(void)
{
  if (pending) input_mode();
  pending = false;
}
