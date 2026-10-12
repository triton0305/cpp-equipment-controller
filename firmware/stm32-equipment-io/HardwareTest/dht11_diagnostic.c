#include "dht11_diagnostic.h"
#include <string.h>

#ifndef DHT11_HARDWARE_TEST
#error "DHT11 diagnostics are only for the hardware-test build"
#endif

Dht11Diagnostic dht11_diagnostic;

void Dht11Diagnostic_Reset(void)
{
  dht11_diagnostic = (Dht11Diagnostic){0};
}

void Dht11Diagnostic_Record(const uint8_t raw[5], unsigned bits,
                            Dht11DiagnosticReason reason, uint32_t pulse_us)
{
  memcpy(dht11_diagnostic.raw, raw, sizeof(dht11_diagnostic.raw));
  dht11_diagnostic.bits = bits;
  dht11_diagnostic.raw_available = true;
  dht11_diagnostic.reason = reason;
  dht11_diagnostic.pulse_us = pulse_us;
}

const char *Dht11Diagnostic_ReasonName(Dht11DiagnosticReason reason)
{
  switch (reason)
  {
    case DHT_DIAG_NONE: return "NONE";
    case DHT_DIAG_INIT_ARGUMENT: return "INIT_ARGUMENT";
    case DHT_DIAG_SAMPLE_ARGUMENT: return "SAMPLE_ARGUMENT";
    case DHT_DIAG_FRAME_ARGUMENT: return "FRAME_ARGUMENT";
    case DHT_DIAG_CHECKSUM: return "CHECKSUM";
    case DHT_DIAG_HUMIDITY_DECIMAL: return "HUMIDITY_DECIMAL";
    case DHT_DIAG_TEMPERATURE_DECIMAL: return "TEMPERATURE_DECIMAL";
    case DHT_DIAG_HUMIDITY_RANGE: return "HUMIDITY_RANGE";
    case DHT_DIAG_TEMPERATURE_RANGE: return "TEMPERATURE_RANGE";
    case DHT_DIAG_PULSE: return "PULSE";
    case DHT_DIAG_RESPONSE_TIMEOUT: return "RESPONSE_TIMEOUT";
    case DHT_DIAG_DATA_LOW_TIMEOUT: return "DATA_LOW_TIMEOUT";
    case DHT_DIAG_DATA_HIGH_TIMEOUT: return "DATA_HIGH_TIMEOUT";
    case DHT_DIAG_END_TIMEOUT: return "END_TIMEOUT";
    case DHT_DIAG_START_LATE: return "START_LATE";
  }
  return "UNKNOWN";
}
