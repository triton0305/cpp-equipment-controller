#include "dht11_frame.h"

#ifdef DHT11_HARDWARE_TEST
#include "dht11_diagnostic.h"
#else
#define DHT11_DIAG_REASON(reason_) ((void)0)
#endif

Dht11Result Dht11_Decode(const uint8_t *frame, size_t length, Dht11Sample *sample)
{
  if (!frame || !sample || length != 5U)
  {
    DHT11_DIAG_REASON(DHT_DIAG_FRAME_ARGUMENT);
    return DHT11_INVALID_DATA;
  }
  if ((uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
  {
    DHT11_DIAG_REASON(DHT_DIAG_CHECKSUM);
    return DHT11_CHECKSUM_ERROR;
  }

  const uint8_t fraction = frame[3] & 0x7fU;
  if (frame[1] > 9U || fraction > 9U)
  {
    DHT11_DIAG_REASON(frame[1] > 9U ? DHT_DIAG_HUMIDITY_DECIMAL : DHT_DIAG_TEMPERATURE_DECIMAL);
    return DHT11_INVALID_DATA;
  }
  const uint16_t humidity = (uint16_t)(frame[0] * 10U + frame[1]);
  int16_t temperature = (int16_t)(frame[2] * 10U + fraction);
  if (frame[3] & 0x80U) temperature = (int16_t)-temperature;
  if (humidity < 50U || humidity > 950U || temperature < -200 || temperature > 600)
  {
    DHT11_DIAG_REASON(humidity < 50U || humidity > 950U ?
                      DHT_DIAG_HUMIDITY_RANGE : DHT_DIAG_TEMPERATURE_RANGE);
    return DHT11_INVALID_DATA;
  }

  sample->humidity_tenths_percent = humidity;
  sample->temperature_tenths_c = temperature;
  return DHT11_OK;
}
