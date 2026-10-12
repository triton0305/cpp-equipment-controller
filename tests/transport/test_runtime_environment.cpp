#include "runtime_fixture.hpp"
#include "io_protocol.h"
#include <sstream>

struct Capture {
  std::ostringstream out;
  std::streambuf* old = std::cout.rdbuf(out.rdbuf());
  ~Capture() { std::cout.rdbuf(old); }
};
static void at(Fixture& f, unsigned ms, const std::string& data = "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:70\n")
{
  f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(ms);
  f.receive(data); f.endpoint->drain();
}
int main()
{
  try
  {
    SensorState sensor;
    CHECK(!sensor.temperature_valid && !sensor.humidity_valid);
    for (auto value : {"-0.1", "25.9", "0", "60.0"})
    {
      CHECK(AsciiProtocol::parseSensorLine(std::string("TEMP:") + value, sensor));
      CHECK(sensor.temperature_valid && sensor.temperature == std::stod(value));
    }
    for (auto value : {"0", "45.2", "100.0"})
    {
      CHECK(AsciiProtocol::parseSensorLine(std::string("HUMIDITY:") + value, sensor));
      CHECK(sensor.humidity_valid && sensor.humidity == std::stod(value));
    }
    for (const auto key : {"TEMP:", "HUMIDITY:"})
      for (const auto invalid : {"", "nan", "inf", "-inf", "1.2junk", "--1", " 25", "0x10", "1e999"})
      {
        CHECK(AsciiProtocol::parseSensorLine(std::string(key) + "25.9", sensor));
        CHECK(!AsciiProtocol::parseSensorLine(std::string(key) + invalid, sensor));
        CHECK(std::string(key) == "TEMP:" ? !sensor.temperature_valid : !sensor.humidity_valid);
        CHECK(sensor.temperature == 25.9);
      }
    CHECK(!AsciiProtocol::parseSensorLine("HUMIDITY:-0.1", sensor));
    CHECK(!AsciiProtocol::parseSensorLine("HUMIDITY:100.1", sensor));
    CHECK(AsciiProtocol::parseSensorLine("TEMP:INVALID", sensor) && !sensor.temperature_valid);
    CHECK(AsciiProtocol::parseSensorLine("HUMIDITY:INVALID", sensor) && !sensor.humidity_valid);

    Capture capture;
    Fixture f; f.runtime->poll(); f.endpoint->drain();
    IoSensors m{}; m.door_closed = true; m.pressure_valid = true; m.pressure = 70;
    m.environment_present = m.environment_valid = true;
    m.temperature_tenths_c = 259; m.humidity_tenths_percent = 452;
    char packet[128];
    const auto length = IoProtocol_Serialize(&m, packet, sizeof(packet)); CHECK(length > 0);
    at(f, 100, std::string(packet, length));
    CHECK(f.core.getSensorState().temperature == 25.9 && f.core.getSensorState().humidity == 45.2);
    CHECK(f.core.getSensorState().temperature_valid && f.core.getSensorState().humidity_valid);
    for (unsigned ms = 200; ms <= 1000; ms += 100) at(f, ms);
    CHECK(capture.out.str().find("Temperature=25.9 Humidity=45.2") != std::string::npos);
    // Door/pressure traffic never rejuvenates old DHT measurements.
    for (unsigned ms = 1100; ms <= 5000; ms += 100) at(f, ms);
    at(f, 5099); CHECK(f.core.getSensorState().temperature_valid && f.core.getSensorState().humidity_valid);
    at(f, 5100); CHECK(!f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    CHECK(f.core.getSensorState().communication_ok);
    at(f, 5200, "TEMP:26.4\nHUMIDITY:51.7\n");
    CHECK(f.core.getSensorState().temperature_valid && f.core.getSensorState().humidity_valid);
    at(f, 5210, "TEMP:INVALID\nHUMIDITY:INVALID\n");
    CHECK(!f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    CHECK(f.core.getSensorState().temperature == 26.4 && f.core.getSensorState().humidity == 51.7);
    for (unsigned ms = 5300; ms <= 6000; ms += 100) at(f, ms);
    CHECK(capture.out.str().rfind("Temperature=unavailable Humidity=unavailable") != std::string::npos);
    at(f, 6010, "TEMP:25.9\nHUMIDITY:45.2\n");
    at(f, 6020, "TEMP:nan\nHUMIDITY:101\n");
    CHECK(!f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    at(f, 6100, "TEMP:25.9\nHUMIDITY:45.2\n");
    for (unsigned ms = 6200; ms <= 11000; ms += 100) at(f, ms);
    at(f, 11099, "TEMP:26.0\n");
    at(f, 11100);
    CHECK(f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    at(f, 11200, "HUMIDITY:50.0\n");
    f.at(12200); f.endpoint->drain(); fault(f.core);
    CHECK(!f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    f.at(12700); f.endpoint->drain(); at(f, 12710);
    CHECK(!f.core.getSensorState().temperature_valid && !f.core.getSensorState().humidity_valid);
    at(f, 12800, "TEMP:24.1\nHUMIDITY:49.3\n");
    CHECK(f.core.getSensorState().temperature_valid && f.core.getSensorState().humidity_valid);
    // INVALID messages alone do not rescue sensor communication at the deadline.
    at(f, 13799, "TEMP:INVALID\nHUMIDITY:INVALID\n");
    f.at(13800); f.endpoint->drain(); fault(f.core);
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
  std::cout << "DHT11 UART -> SensorState -> snapshot Host PASS\n";
}
