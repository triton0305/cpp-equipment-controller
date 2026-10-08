#ifndef IO_PROTOCOL_H
#define IO_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { IO_PUMP, IO_HEATER, IO_VALVE, IO_BUZZER, IO_RUN_LED, IO_ERROR_LED } IoCommandKind;
typedef struct { IoCommandKind kind; bool on; } IoCommand;
typedef struct { char data[32]; size_t length; bool discard; } IoLineReader;
typedef struct { bool door_closed, estop, pressure_valid; uint16_t pressure; } IoSensors;
/* Feed returns a command only at LF. CR is accepted only immediately before LF. */
bool IoProtocol_Feed(IoLineReader *reader, uint8_t byte, IoCommand *command);
void IoProtocol_Discard(IoLineReader *reader);
bool IoProtocol_Parse(const char *line, IoCommand *command);
/* Returns zero on insufficient capacity/invalid pressure. Never emits invented TEMP/MOTOR values. */
size_t IoProtocol_Serialize(const IoSensors *sensors, char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
