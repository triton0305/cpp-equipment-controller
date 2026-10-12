#include "dht11_hardware_test.h"
#include "dht11.h"
#include "dht11_diagnostic.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef DHT11_HARDWARE_TEST
#error "This harness must only be compiled for the dedicated hardware-test build"
#endif

static UART_HandleTypeDef *debug_uart;
static bool initialized, pending;
static uint32_t sequence, started_ms;
/* Inspect with a debugger if the UART itself fails. Never actuate an output. */
volatile uint32_t dht11_hardware_uart_errors;

static const char *result_name(Dht11Result result)
{
  switch (result)
  {
    case DHT11_OK: return "OK";
    case DHT11_RESPONSE_TIMEOUT: return "RESPONSE_TIMEOUT";
    case DHT11_DATA_TIMEOUT: return "DATA_TIMEOUT";
    case DHT11_CHECKSUM_ERROR: return "CHECKSUM_ERROR";
    case DHT11_INVALID_DATA: return "INVALID_DATA";
    case DHT11_NOT_READY: return "NOT_READY";
    case DHT11_BUSY: return "BUSY";
    case DHT11_HAL_ERROR: return "HAL_ERROR";
  }
  return "UNKNOWN";
}

static void debug_line(const char *line)
{
  /* Only called before a request or after capture, never during Start LOW/data. */
  if (HAL_UART_Transmit(debug_uart, (uint8_t *)line, (uint16_t)strlen(line), 100U) != HAL_OK)
    ++dht11_hardware_uart_errors;
}

void Dht11HardwareTest_Init(TIM_HandleTypeDef *timer, UART_HandleTypeDef *uart)
{
  debug_uart = uart;
  pending = false;
  sequence = 0;
  dht11_hardware_uart_errors = 0;
  Dht11Diagnostic_Reset();
  const Dht11Result result = Dht11_Init(timer);
  initialized = result == DHT11_OK;
  char line[128];
  (void)snprintf(line, sizeof(line),
    "DHT11:HARNESS,INIT=%s,REASON=%s,STABILIZE_MS=%lu,INTERVAL_MS=%lu\r\n",
    result_name(result), Dht11Diagnostic_ReasonName(dht11_diagnostic.reason), (unsigned long)DHT11_STABILIZE_MS,
    (unsigned long)DHT11_INTERVAL_MS);
  debug_line(line);
  /* Init failure stays idle until reset; it cannot produce an OK sample line. */
}

void Dht11HardwareTest_Poll(void)
{
  if (!initialized) return;
  if (!pending)
  {
    Dht11Diagnostic_Reset();
    const Dht11Result result = Dht11_Request();
    if (result != DHT11_BUSY) return;
    started_ms = HAL_GetTick();
    ++sequence;
    pending = true;
  }

  Dht11Sample sample;
  const Dht11Result result = Dht11_Poll(&sample);
  if (result == DHT11_BUSY) return;
  const uint32_t done_ms = HAL_GetTick();
  pending = false;
  char line[256];
  if (result == DHT11_OK)
  {
    const int temperature = sample.temperature_tenths_c;
    const unsigned magnitude = (unsigned)(temperature < 0 ? -temperature : temperature);
    (void)snprintf(line, sizeof(line),
      "DHT11:OK,SEQ=%lu,START_MS=%lu,DONE_MS=%lu,TEMP=%s%u.%u,HUM=%u.%u,CHECKSUM=VALID\r\n",
      (unsigned long)sequence, (unsigned long)started_ms, (unsigned long)done_ms,
      temperature < 0 ? "-" : "", magnitude / 10U, magnitude % 10U,
      (unsigned)sample.humidity_tenths_percent / 10U,
      (unsigned)sample.humidity_tenths_percent % 10U);
  }
  else
  {
    const Dht11Diagnostic *d = &dht11_diagnostic;
    if (d->raw_available)
    {
      const uint8_t sum = (uint8_t)(d->raw[0] + d->raw[1] + d->raw[2] + d->raw[3]);
      (void)snprintf(line, sizeof(line),
        "DHT11:%s,SEQ=%lu,START_MS=%lu,DONE_MS=%lu,REASON=%s,FRAME=%s,BITS=%u,"
        "RAW=%02X-%02X-%02X-%02X-%02X,CHECKSUM_RX=%02X,CHECKSUM_CALC=%02X\r\n",
        result_name(result), (unsigned long)sequence,
        (unsigned long)started_ms, (unsigned long)done_ms,
        Dht11Diagnostic_ReasonName(d->reason), d->bits == 40U ? "COMPLETE" : "PARTIAL",
        d->bits, (unsigned)d->raw[0], (unsigned)d->raw[1], (unsigned)d->raw[2],
        (unsigned)d->raw[3], (unsigned)d->raw[4], (unsigned)d->raw[4], (unsigned)sum);
    }
    else
    {
      (void)snprintf(line, sizeof(line),
        "DHT11:%s,SEQ=%lu,REASON=%s,FRAME=NONE\r\n", result_name(result),
        (unsigned long)sequence, Dht11Diagnostic_ReasonName(d->reason));
    }
  }
  debug_line(line);
  if (result != DHT11_OK && dht11_diagnostic.reason == DHT_DIAG_PULSE)
  {
    (void)snprintf(line, sizeof(line),
      "DHT11:DIAG,SEQ=%lu,REASON=PULSE,BIT_INDEX=%u,PULSE_US=%lu,"
      "BIT0_US=15..40,BIT1_US=55..90,DECODED_BIT=NONE\r\n",
      (unsigned long)sequence, dht11_diagnostic.bits,
      (unsigned long)dht11_diagnostic.pulse_us);
    debug_line(line);
  }
}
