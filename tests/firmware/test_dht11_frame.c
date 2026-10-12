#include "dht11_frame.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void check_frame(uint8_t h, uint8_t hf, uint8_t t, uint8_t tf,
                        Dht11Result expected, int humidity, int temperature)
{
  uint8_t frame[5] = {h, hf, t, tf, (uint8_t)(h + hf + t + tf)};
  Dht11Sample sample = {123, -123};
  CHECK(Dht11_Decode(frame, sizeof(frame), &sample) == expected);
  CHECK(sample.humidity_tenths_percent == (expected == DHT11_OK ? humidity : 123));
  CHECK(sample.temperature_tenths_c == (expected == DHT11_OK ? temperature : -123));
}

int main(void)
{
  check_frame(53, 0, 24, 0, DHT11_OK, 530, 240);
  check_frame(61, 0, 23, 7, DHT11_OK, 610, 237);
  check_frame(53, 0, 4, 0x85, DHT11_OK, 530, -45);
  check_frame(5, 0, 20, 0x80, DHT11_OK, 50, -200);
  check_frame(95, 0, 60, 0, DHT11_OK, 950, 600);
  /* Sum >255: checksum wraps modulo 256, including the temperature sign bit. */
  check_frame(95, 0, 19, 0x89, DHT11_OK, 950, -199);
  check_frame(4, 0, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(96, 0, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(53, 1, 24, 0, DHT11_OK, 531, 240);
  check_frame(94, 9, 24, 0, DHT11_OK, 949, 240);
  check_frame(53, 10, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(53, 255, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(4, 9, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(95, 1, 24, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(53, 0, 24, 10, DHT11_INVALID_DATA, 0, 0);
  check_frame(53, 0, 20, 0x81, DHT11_INVALID_DATA, 0, 0);
  check_frame(53, 0, 60, 1, DHT11_INVALID_DATA, 0, 0);
  check_frame(0, 0, 0, 0, DHT11_INVALID_DATA, 0, 0);
  check_frame(255, 255, 255, 255, DHT11_INVALID_DATA, 0, 0);
  /* Literal regression frame: checksum 0x51, humidity 45.2%, temperature 25.9C. */
  uint8_t fractional_frame[5] = {0x2D, 0x02, 0x19, 0x09, 0x51};
  Dht11Sample fractional_sample = {0};
  CHECK(Dht11_Decode(fractional_frame, sizeof(fractional_frame), &fractional_sample) == DHT11_OK);
  CHECK(fractional_sample.humidity_tenths_percent == 452);
  CHECK(fractional_sample.temperature_tenths_c == 259);
  fractional_frame[4] ^= 1U;
  CHECK(Dht11_Decode(fractional_frame, sizeof(fractional_frame), &fractional_sample) == DHT11_CHECKSUM_ERROR);
  CHECK(fractional_sample.humidity_tenths_percent == 452 && fractional_sample.temperature_tenths_c == 259);
  uint8_t frame[6] = {53, 0, 24, 0, 77, 0};
  Dht11Sample sample = {123, -123};
  CHECK(Dht11_Decode(NULL, 5, &sample) == DHT11_INVALID_DATA);
  CHECK(Dht11_Decode(frame, 5, NULL) == DHT11_INVALID_DATA);
  for (size_t n = 0; n < 5; ++n)
    CHECK(Dht11_Decode(frame, n, &sample) == DHT11_INVALID_DATA);
  CHECK(Dht11_Decode(frame, 6, &sample) == DHT11_INVALID_DATA);
  frame[4] ^= 1U;
  CHECK(Dht11_Decode(frame, 5, &sample) == DHT11_CHECKSUM_ERROR);
  CHECK(sample.humidity_tenths_percent == 123 && sample.temperature_tenths_c == -123);
  puts("DHT11 frame decode: PASS");
  return 0;
}
