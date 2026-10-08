#include "io_protocol.h"
#include "ascii_protocol.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
int main()
{
  // Cross-check all 64 actual Linux serializer output combinations, no duplicated fixtures.
  for (unsigned mask = 0; mask < 64; ++mask)
  {
    DeviceState d;
    d.pump_on = mask & 1; d.heater_on = mask & 2; d.valve_open = mask & 4;
    d.buzzer_on = mask & 8; d.run_led = mask & 16; d.error_led = mask & 32;
    IoLineReader reader{}; IoCommand command{}; unsigned count = 0;
    for (unsigned char c : AsciiProtocol::serializeDeviceState(d))
      if (IoProtocol_Feed(&reader, c, &command))
      {
        CHECK(static_cast<unsigned>(command.kind) == count);
        CHECK(command.on == static_cast<bool>(mask & (1U << count)));
        ++count;
      }
    CHECK(count == 6);
  }
  for (unsigned bits = 0; bits < 4; ++bits)
    for (uint16_t raw : {0, 2048, 4095})
    {
      IoSensors s{bool(bits & 1), bool(bits & 2), true, raw}; char packet[64];
      auto n = IoProtocol_Serialize(&s, packet, sizeof(packet)); CHECK(n > 0);
      SensorState linux_sensor;
      std::istringstream lines(std::string(packet, n)); std::string line;
      while (std::getline(lines, line)) CHECK(AsciiProtocol::parseSensorLine(line, linux_sensor));
      CHECK(linux_sensor.door_closed == s.door_closed);
      CHECK(linux_sensor.emergency_stop == s.estop);
      CHECK(linux_sensor.pressure == raw);
    }
  IoLineReader reader{}; IoCommand command{IO_HEATER, true};
  auto feed = [&](const std::string &s) { unsigned n = 0; for (unsigned char c : s) n += IoProtocol_Feed(&reader, c, &command); return n; };
  for (auto bad : {"", "pump:ON", "PUMP:ONx", "PUMP: ON", "VALVE:ON", "TEMP:25", "RUN_LED:OFF\rX"})
    CHECK(feed(std::string(bad) + "\n") == 0);
  CHECK(command.kind == IO_HEATER && command.on);
  CHECK(feed("PUMP:") == 0); CHECK(feed("ON\r\n") == 1);
  CHECK(feed(std::string("PUMP:ON\0junk\n", 13)) == 0);
  CHECK(feed(std::string(100, 'x') + "PUMP:ON\n") == 0);
  CHECK(feed("PUMP:OFF\n") == 1 && !command.on);
  CHECK(feed("PUMP:") == 0); IoProtocol_Discard(&reader);
  CHECK(feed("ON\n") == 0); CHECK(feed("HEATER:ON\n") == 1);
  IoSensors s{false, true, false, 0}; char out[64];
  CHECK(IoProtocol_Serialize(&s, out, sizeof(out)) > 0);
  CHECK(std::string(out) == "DOOR:OPEN\nESTOP:ON\n");
  CHECK(IoProtocol_Serialize(&s, out, 4) == 0);
  s.pressure_valid = true; s.pressure = 4096;
  CHECK(IoProtocol_Serialize(&s, out, sizeof(out)) == 0);
}
