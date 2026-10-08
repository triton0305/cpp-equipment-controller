#include "io_protocol.h"
#include <stdio.h>
#include <string.h>

bool IoProtocol_Parse(const char *line, IoCommand *command)
{
  static const struct { const char *yes, *no; IoCommandKind kind; } formats[] = {
    {"PUMP:ON", "PUMP:OFF", IO_PUMP},
    {"HEATER:ON", "HEATER:OFF", IO_HEATER},
    {"VALVE:OPEN", "VALVE:CLOSED", IO_VALVE},
    {"BUZZER:ON", "BUZZER:OFF", IO_BUZZER},
    {"RUN_LED:ON", "RUN_LED:OFF", IO_RUN_LED},
    {"ERROR_LED:ON", "ERROR_LED:OFF", IO_ERROR_LED}
  };
  for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
  {
    bool on = strcmp(line, formats[i].yes) == 0;
    if (on || strcmp(line, formats[i].no) == 0)
    {
      command->kind = formats[i].kind;
      command->on = on;
      return true;
    }
  }
  return false;
}

void IoProtocol_Discard(IoLineReader *reader)
{
  reader->length = 0;
  reader->discard = true;
}

bool IoProtocol_Feed(IoLineReader *reader, uint8_t byte, IoCommand *command)
{
  if (byte == '\n')
  {
    bool accepted = false;
    if (!reader->discard)
    {
      if (reader->length && reader->data[reader->length - 1] == '\r') --reader->length;
      reader->data[reader->length] = '\0';
      accepted = IoProtocol_Parse(reader->data, command);
    }
    reader->length = 0;
    reader->discard = false;
    return accepted;
  }
  if (reader->discard) return false;
  if (byte == 0 || (byte < 32 && byte != '\r') || byte > 126 ||
      reader->length == sizeof(reader->data) - 1)
  {
    IoProtocol_Discard(reader);
    return false;
  }
  reader->data[reader->length++] = (char)byte;
  return false;
}

size_t IoProtocol_Serialize(const IoSensors *s, char *out, size_t capacity)
{
  if (s->pressure_valid && s->pressure > 4095) return 0;
  int n = snprintf(out, capacity, "DOOR:%s\nESTOP:%s\n",
                   s->door_closed ? "CLOSED" : "OPEN", s->estop ? "ON" : "OFF");
  if (n < 0 || (size_t)n >= capacity) return 0;
  size_t used = (size_t)n;
  if (s->pressure_valid)
  {
    n = snprintf(out + used, capacity - used, "PRESSURE:%u\n", (unsigned)s->pressure);
    if (n < 0 || (size_t)n >= capacity - used) return 0;
    used += (size_t)n;
  }
  return used;
}
