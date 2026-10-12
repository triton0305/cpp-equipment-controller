#include "dht11.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

GPIO_TypeDef fake_gpio[3];
TIM_TypeDef fake_tim5;
static uint32_t tick, counter, released_at, mode;
static unsigned reads, writes;
static bool stopped;
static HAL_StatusTypeDef start_result;
enum { LINE_HIGH, LINE_LOW, RESPONSE_ONLY, BAD_FIRST_BIT };
static int signal_kind;
uint32_t HAL_GetTick(void) { if (stopped) ++tick; return tick; }
uint32_t fake_dht11_counter(void) { if (!stopped) ++counter; return counter; }
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
  CHECK(timer->Instance == TIM5);
  return start_result;
}
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio)
{
  CHECK(port == GPIOC && gpio->Pin == GPIO_PIN_4 && gpio->Pull == GPIO_NOPULL);
  mode = gpio->Mode;
  if (mode == GPIO_MODE_INPUT) released_at = counter;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
  CHECK(port == GPIOC && pin == GPIO_PIN_4 && state == GPIO_PIN_RESET);
  ++writes;
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  CHECK(port == GPIOC && pin == GPIO_PIN_4 && mode == GPIO_MODE_INPUT);
  ++reads;
  if (signal_kind == LINE_HIGH) return GPIO_PIN_SET;
  if (signal_kind == LINE_LOW) return GPIO_PIN_RESET;
  /* Only response and one malformed bit; full waveform stays a hardware test. */
  const uint32_t elapsed = (uint32_t)(counter - released_at);
  if (elapsed < 15U) return GPIO_PIN_SET;
  if (elapsed < 95U) return GPIO_PIN_RESET;
  if (elapsed < 175U) return GPIO_PIN_SET;
  if (signal_kind == BAD_FIRST_BIT && elapsed >= 225U && elapsed < 273U)
    return GPIO_PIN_SET;
  return GPIO_PIN_RESET;
}

int main(void)
{
  TIM_HandleTypeDef timer = {TIM5, {83U, UINT32_MAX, TIM_COUNTERMODE_UP}};
  Dht11Sample sample = {123, -123};
  CHECK(Dht11_Request() == DHT11_NOT_READY);
  CHECK(Dht11_Poll(&sample) == DHT11_NOT_READY);
  CHECK(Dht11_Init(NULL) == DHT11_INVALID_DATA);
  timer.Init.Prescaler = 84;
  CHECK(Dht11_Init(&timer) == DHT11_INVALID_DATA);
  timer.Init.Prescaler = 83;
  start_result = HAL_ERROR;
  CHECK(Dht11_Init(&timer) == DHT11_HAL_ERROR);
  CHECK(Dht11_Request() == DHT11_NOT_READY);
  start_result = HAL_OK;
  tick = UINT32_MAX - 1000U;
  CHECK(Dht11_Init(&timer) == DHT11_OK);
  CHECK(mode == GPIO_MODE_INPUT);
  tick += DHT11_STABILIZE_MS - 1U;
  CHECK(Dht11_Request() == DHT11_NOT_READY && writes == 0);
  ++tick; /* ms rollover during stabilization */
  CHECK(Dht11_Request() == DHT11_BUSY && writes == 1);
  CHECK(mode == GPIO_MODE_OUTPUT_PP);
  CHECK(Dht11_Request() == DHT11_BUSY && writes == 1);
  tick += 19U;
  CHECK(Dht11_Poll(&sample) == DHT11_BUSY && reads == 0);
  ++tick;
  CHECK(Dht11_Poll(&sample) == DHT11_RESPONSE_TIMEOUT);
  CHECK(mode == GPIO_MODE_INPUT);
  CHECK(Dht11_Poll(&sample) == DHT11_NOT_READY);
  CHECK(Dht11_Request() == DHT11_NOT_READY);

  for (int kind = LINE_LOW; kind <= BAD_FIRST_BIT; ++kind)
  {
    signal_kind = kind;
    tick += DHT11_INTERVAL_MS;
    CHECK(Dht11_Request() == DHT11_BUSY);
    tick += 20U;
    counter = UINT32_MAX - 50U; /* us rollover during response */
    const Dht11Result expected = kind == LINE_LOW ? DHT11_RESPONSE_TIMEOUT :
      kind == RESPONSE_ONLY ? DHT11_DATA_TIMEOUT : DHT11_INVALID_DATA;
    CHECK(Dht11_Poll(&sample) == expected);
    CHECK(mode == GPIO_MODE_INPUT);
  }
  tick += DHT11_INTERVAL_MS;
  CHECK(Dht11_Request() == DHT11_BUSY);
  tick += 30U;
  CHECK(Dht11_Poll(&sample) == DHT11_DATA_TIMEOUT && mode == GPIO_MODE_INPUT);
  tick += DHT11_INTERVAL_MS;
  CHECK(Dht11_Request() == DHT11_BUSY);
  Dht11_Cancel();
  CHECK(mode == GPIO_MODE_INPUT && Dht11_Request() == DHT11_NOT_READY);
  tick += DHT11_INTERVAL_MS;
  CHECK(Dht11_Request() == DHT11_BUSY);
  CHECK(Dht11_Poll(NULL) == DHT11_INVALID_DATA && mode == GPIO_MODE_INPUT);
  tick += DHT11_INTERVAL_MS;
  CHECK(Dht11_Request() == DHT11_BUSY);
  tick += 20U;
  stopped = true;
  signal_kind = LINE_HIGH;
  CHECK(Dht11_Poll(&sample) == DHT11_RESPONSE_TIMEOUT);
  stopped = false;
  CHECK(sample.humidity_tenths_percent == 123 && sample.temperature_tenths_c == -123);
  puts("DHT11 lifecycle and timeout handling: PASS");
  return 0;
}
