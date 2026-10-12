#ifndef DHT11_FRAME_H
#define DHT11_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DHT11_OK,
  DHT11_RESPONSE_TIMEOUT,
  DHT11_DATA_TIMEOUT,
  DHT11_CHECKSUM_ERROR,
  DHT11_INVALID_DATA,
  DHT11_NOT_READY,
  DHT11_BUSY,
  DHT11_HAL_ERROR
} Dht11Result;

typedef struct {
  uint16_t humidity_tenths_percent;
  int16_t temperature_tenths_c;
} Dht11Sample;

/* DHT11: humidity and temperature decimal digits 0..9,
 * temperature sign in byte 3 bit 7. Output changes only on OK. */
Dht11Result Dht11_Decode(const uint8_t *frame, size_t length, Dht11Sample *sample);

#ifdef __cplusplus
}
#endif
#endif
