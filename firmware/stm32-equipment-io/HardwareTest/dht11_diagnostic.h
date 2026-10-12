#ifndef DHT11_DIAGNOSTIC_H
#define DHT11_DIAGNOSTIC_H

#include <stdbool.h>
#include <stdint.h>

/* Bench-only observation. No thresholds, driver results or production API change. */
typedef enum {
  DHT_DIAG_NONE,
  DHT_DIAG_INIT_ARGUMENT,
  DHT_DIAG_SAMPLE_ARGUMENT,
  DHT_DIAG_FRAME_ARGUMENT,
  DHT_DIAG_CHECKSUM,
  DHT_DIAG_HUMIDITY_DECIMAL,
  DHT_DIAG_TEMPERATURE_DECIMAL,
  DHT_DIAG_HUMIDITY_RANGE,
  DHT_DIAG_TEMPERATURE_RANGE,
  DHT_DIAG_PULSE,
  DHT_DIAG_RESPONSE_TIMEOUT,
  DHT_DIAG_DATA_LOW_TIMEOUT,
  DHT_DIAG_DATA_HIGH_TIMEOUT,
  DHT_DIAG_END_TIMEOUT,
  DHT_DIAG_START_LATE
} Dht11DiagnosticReason;

typedef struct {
  uint8_t raw[5];
  unsigned bits; /* Accepted bits; partial final byte is NOT a complete byte. */
  bool raw_available;
  uint32_t pulse_us; /* Valid only for PULSE; failing bit index equals bits. */
  Dht11DiagnosticReason reason;
} Dht11Diagnostic;

extern Dht11Diagnostic dht11_diagnostic;
void Dht11Diagnostic_Reset(void);
/* Call only on terminal capture paths, never between successfully captured bits. */
void Dht11Diagnostic_Record(const uint8_t raw[5], unsigned bits,
                            Dht11DiagnosticReason reason, uint32_t pulse_us);
const char *Dht11Diagnostic_ReasonName(Dht11DiagnosticReason reason);
#define DHT11_DIAG_REASON(reason_) (dht11_diagnostic.reason = (reason_))
#define DHT11_DIAG_RECORD(raw_, bits_, reason_, width_) \
  Dht11Diagnostic_Record(raw_, bits_, reason_, width_)
#endif
