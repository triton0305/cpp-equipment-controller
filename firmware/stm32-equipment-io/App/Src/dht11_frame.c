#include "dht11_frame.h"

Dht11Result Dht11_Decode(const uint8_t *frame, size_t length, Dht11Sample *sample)
{
  if (!frame || !sample || length != 5U) return DHT11_INVALID_DATA;
  if ((uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]) != frame[4])
    return DHT11_CHECKSUM_ERROR;

  const uint8_t fraction = frame[3] & 0x7fU;
  if (frame[1] != 0U || fraction > 9U) return DHT11_INVALID_DATA;
  const uint16_t humidity = (uint16_t)(frame[0] * 10U);
  int16_t temperature = (int16_t)(frame[2] * 10U + fraction);
  if (frame[3] & 0x80U) temperature = (int16_t)-temperature;
  if (humidity < 50U || humidity > 950U || temperature < -200 || temperature > 600)
    return DHT11_INVALID_DATA;

  sample->humidity_tenths_percent = humidity;
  sample->temperature_tenths_c = temperature;
  return DHT11_OK;
}
