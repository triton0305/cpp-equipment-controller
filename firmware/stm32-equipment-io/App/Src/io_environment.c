#include "io_environment.h"
#include "dht11.h"
#include "uart_io.h"

static bool enabled, valid, pending;
static uint32_t measured_at;
static Dht11Sample measurement;

void IoEnvironment_Reset(void)
{
  Dht11_Cancel();
  Dht11_SetServiceHook(NULL);
  enabled = valid = pending = false;
}

void IoEnvironment_Init(TIM_HandleTypeDef *timer)
{
  IoEnvironment_Reset();
  enabled = Dht11_Init(timer) == DHT11_OK;
  if (enabled) Dht11_SetServiceHook(UartIo_BufferRx);
  pending = true; // Startup / init failure has no measurement yet.
}

void IoEnvironment_Poll(void)
{
  if (!enabled) return;
  Dht11Sample sample;
  const Dht11Result result = Dht11_Poll(&sample);
  if (result == DHT11_OK)
  {
    measurement = sample;
    measured_at = HAL_GetTick();
    valid = pending = true;
  }
  else if (result != DHT11_BUSY && result != DHT11_NOT_READY)
  {
    valid = false;
    pending = true;
  }
  // Driver enforces stabilization and >=2.1 s between attempts, including failures.
  (void)Dht11_Request();
}

bool IoEnvironment_Valid(uint32_t now)
{
  if (valid && (uint32_t)(now - measured_at) >= IO_ENVIRONMENT_TIMEOUT_MS)
  {
    valid = false;
    pending = true;
  }
  return valid;
}

void IoEnvironment_Take(IoSensors *sensors, uint32_t now)
{
  (void)IoEnvironment_Valid(now);
  sensors->environment_present = pending;
  sensors->environment_valid = valid;
  if (pending && valid)
  {
    sensors->temperature_tenths_c = measurement.temperature_tenths_c;
    sensors->humidity_tenths_percent = measurement.humidity_tenths_percent;
  }
  pending = false;
}
