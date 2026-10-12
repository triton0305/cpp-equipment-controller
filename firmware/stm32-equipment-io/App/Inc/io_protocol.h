#ifndef IO_PROTOCOL_H
#define IO_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  IO_PUMP,
  IO_HEATER,
  IO_VALVE,
  IO_BUZZER,
  IO_RUN_LED,
  IO_ERROR_LED,
  IO_STATE
} IoCommandKind;

typedef enum
{
  IO_STATE_SAFE,
  IO_STATE_IDLE,
  IO_STATE_READY,
  IO_STATE_RUN,
  IO_STATE_ERROR
} IoState;

typedef struct
{
  IoCommandKind kind;
  bool on;
  IoState state;
} IoCommand;
typedef struct { char data[32]; size_t length; bool discard; } IoLineReader;
typedef struct {
  bool door_closed, estop, pressure_valid;
  uint16_t pressure;
  bool environment_present, environment_valid;
  int16_t temperature_tenths_c;
  uint16_t humidity_tenths_percent;
} IoSensors;
/* Feed returns a command only at LF. CR is accepted only immediately before LF. */
bool IoProtocol_Feed(IoLineReader *reader, uint8_t byte, IoCommand *command);
void IoProtocol_Discard(IoLineReader *reader);
bool IoProtocol_Parse(const char *line, IoCommand *command);
/* Returns zero on insufficient capacity/invalid pressure. Environment fields are emitted only for a new result; invalid reports contain no values. */
size_t IoProtocol_Serialize(const IoSensors *sensors, char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
