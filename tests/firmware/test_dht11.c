#include "dht11.h"
#include "main.h"
#ifdef DHT11_HARDWARE_TEST
#include "dht11_diagnostic.h"
#include "dht11_hardware_test.h"
#include <string.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

GPIO_TypeDef fake_gpio[3];
TIM_TypeDef fake_tim5;
static uint32_t tick, counter, released_at, mode;
static unsigned reads, writes;
static bool stopped;
static HAL_StatusTypeDef start_result;
enum { LINE_HIGH, LINE_LOW, RESPONSE_ONLY, BAD_FIRST_BIT, FRAME_BAD_HUMIDITY, FRAME_BAD_CHECKSUM, FRAME_FRACTIONAL_OK };
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
#ifdef DHT11_HARDWARE_TEST
  /* Deterministic frames verify complete RAW diagnostics and fractional samples. */
  if (signal_kind >= FRAME_BAD_HUMIDITY && signal_kind <= FRAME_FRACTIONAL_OK)
  {
    const uint8_t invalid_raw[5] = {53, 10, 24, 0, signal_kind == FRAME_BAD_HUMIDITY ? 87 : 88};
    const uint8_t valid_raw[5] = {0x2D, 0x02, 0x19, 0x09, 0x51};
    const uint8_t *raw = signal_kind == FRAME_FRACTIONAL_OK ? valid_raw : invalid_raw;
    uint32_t edge = 175U;
    for (unsigned bit = 0; bit < 40U; ++bit)
    {
      edge += 50U;
      if (elapsed < edge) return GPIO_PIN_RESET;
      edge += (raw[bit / 8U] & (0x80U >> (bit % 8U))) ? 70U : 26U;
      if (elapsed < edge) return GPIO_PIN_SET;
    }
    return elapsed < edge + 50U ? GPIO_PIN_RESET : GPIO_PIN_SET;
  }
#endif
  if (signal_kind == BAD_FIRST_BIT && elapsed >= 225U && elapsed < 273U)
    return GPIO_PIN_SET;
  return GPIO_PIN_RESET;
}

#ifdef DHT11_HARDWARE_TEST
static char debug_output[1024];
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                    uint16_t size, uint32_t timeout)
{
  (void)uart;
  CHECK(mode == GPIO_MODE_INPUT); /* Never transmit during Start LOW. */
  CHECK(timeout == 100U);
  const size_t used = strlen(debug_output);
  CHECK(used + size < sizeof(debug_output));
  memcpy(debug_output + used, data, size);
  debug_output[used + size] = '\0';
  return HAL_OK;
}

static void check_decode_reasons(void)
{
  const struct {
    uint8_t raw[5];
    Dht11Result result;
    Dht11DiagnosticReason reason;
  } cases[] = {
    {{53, 0, 24, 0, 77}, DHT11_OK, DHT_DIAG_NONE},
    {{53, 1, 24, 0, 78}, DHT11_OK, DHT_DIAG_NONE},
    {{0x2D, 0x02, 0x19, 0x09, 0x51}, DHT11_OK, DHT_DIAG_NONE},
    {{53, 10, 24, 0, 87}, DHT11_INVALID_DATA, DHT_DIAG_HUMIDITY_DECIMAL},
    {{53, 0, 24, 10, 87}, DHT11_INVALID_DATA, DHT_DIAG_TEMPERATURE_DECIMAL},
    {{4, 0, 24, 0, 28}, DHT11_INVALID_DATA, DHT_DIAG_HUMIDITY_RANGE},
    {{53, 0, 60, 1, 114}, DHT11_INVALID_DATA, DHT_DIAG_TEMPERATURE_RANGE},
    /* Checksum must win over decimal/range validation. */
    {{4, 1, 60, 10, 0}, DHT11_CHECKSUM_ERROR, DHT_DIAG_CHECKSUM}
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    Dht11Diagnostic_Reset();
    Dht11Sample sample = {123, -123};
    CHECK(Dht11_Decode(cases[i].raw, 5U, &sample) == cases[i].result);
    CHECK(dht11_diagnostic.reason == cases[i].reason);
    CHECK(strcmp(Dht11Diagnostic_ReasonName(cases[i].reason), "UNKNOWN") != 0);
    if (cases[i].result != DHT11_OK)
      CHECK(sample.humidity_tenths_percent == 123 && sample.temperature_tenths_c == -123);
  }
  Dht11Sample sample;
  CHECK(Dht11_Decode(NULL, 5U, &sample) == DHT11_INVALID_DATA);
  CHECK(dht11_diagnostic.reason == DHT_DIAG_FRAME_ARGUMENT);
  Dht11Diagnostic_Reset();
  CHECK(!dht11_diagnostic.raw_available && dht11_diagnostic.reason == DHT_DIAG_NONE);
}
#endif

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
#ifdef DHT11_HARDWARE_TEST
    CHECK(dht11_diagnostic.raw_available && dht11_diagnostic.bits == 0U);
    CHECK(dht11_diagnostic.reason == (kind == LINE_LOW ? DHT_DIAG_RESPONSE_TIMEOUT :
          kind == RESPONSE_ONLY ? DHT_DIAG_DATA_LOW_TIMEOUT : DHT_DIAG_PULSE));
    if (kind == BAD_FIRST_BIT)
      CHECK(dht11_diagnostic.pulse_us > 40U && dht11_diagnostic.pulse_us < 55U);
#endif
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
#ifdef DHT11_HARDWARE_TEST
  check_decode_reasons();
  UART_HandleTypeDef uart = {0};
  Dht11HardwareTest_Init(&timer, &uart);
  debug_output[0] = '\0';
  signal_kind = BAD_FIRST_BIT;
  tick += DHT11_INTERVAL_MS;
  Dht11HardwareTest_Poll();
  CHECK(debug_output[0] == '\0'); /* No UART while capture is pending. */
  tick += DHT11_START_LOW_MS;
  Dht11HardwareTest_Poll();
  CHECK(strstr(debug_output, "DHT11:INVALID_DATA,SEQ=1,") != NULL);
  CHECK(strstr(debug_output, "REASON=PULSE,FRAME=PARTIAL,BITS=0,RAW=00-00-00-00-00") != NULL);
  CHECK(strstr(debug_output, "BIT_INDEX=0,PULSE_US=") != NULL);
  CHECK(strstr(debug_output, "BIT0_US=15..40,BIT1_US=55..90,DECODED_BIT=NONE\r\n") != NULL);
  debug_output[0] = '\0';
  tick += DHT11_INTERVAL_MS;
  Dht11HardwareTest_Poll();
  tick += 30U;
  Dht11HardwareTest_Poll();
  CHECK(strstr(debug_output, "DHT11:DATA_TIMEOUT,SEQ=2,REASON=START_LATE,FRAME=NONE") != NULL);
  CHECK(strstr(debug_output, "RAW=") == NULL); /* No previous capture leaked. */
  for (int kind = FRAME_BAD_HUMIDITY; kind <= FRAME_FRACTIONAL_OK; ++kind)
  {
    debug_output[0] = '\0';
    signal_kind = kind;
    tick += DHT11_INTERVAL_MS;
    Dht11HardwareTest_Poll();
    tick += DHT11_START_LOW_MS;
    Dht11HardwareTest_Poll();
    CHECK(dht11_diagnostic.bits == 40U);
    if (kind == FRAME_BAD_HUMIDITY)
    {
      CHECK(strstr(debug_output, "DHT11:INVALID_DATA,") != NULL);
      CHECK(strstr(debug_output, "REASON=HUMIDITY_DECIMAL,FRAME=COMPLETE,BITS=40,") != NULL);
      CHECK(strstr(debug_output, "RAW=35-0A-18-00-57,CHECKSUM_RX=57,CHECKSUM_CALC=57\r\n") != NULL);
    }
    else if (kind == FRAME_BAD_CHECKSUM)
    {
      CHECK(strstr(debug_output, "DHT11:CHECKSUM_ERROR,") != NULL);
      CHECK(strstr(debug_output, "REASON=CHECKSUM,FRAME=COMPLETE,BITS=40,") != NULL);
      CHECK(strstr(debug_output, "RAW=35-0A-18-00-58,CHECKSUM_RX=58,CHECKSUM_CALC=57\r\n") != NULL);
    }
    else
    {
      CHECK(strstr(debug_output, "DHT11:OK,") != NULL);
      CHECK(strstr(debug_output, "TEMP=25.9,HUM=45.2,CHECKSUM=VALID\r\n") != NULL);
      CHECK(dht11_diagnostic.reason == DHT_DIAG_NONE);
    }
    CHECK(strstr(debug_output, "PULSE_US=") == NULL);
  }
#endif
  puts("DHT11 lifecycle and timeout handling: PASS");
  return 0;
}
