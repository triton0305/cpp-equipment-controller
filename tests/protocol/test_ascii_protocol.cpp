#include <iostream>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#include "sensor_state.hpp"
#include "ascii_protocol.hpp"


int main()
{
  std::cout << "\n[TEST 16] ASCII temperature parser\n";

  SensorState protocol_sensor;

  const bool valid =
    AsciiProtocol::parseSensorLine("TEMP:26.5", protocol_sensor);

  std::cout
    << "Valid TEMP"
    << " | parsed=" << valid
    << " | temperature=" << protocol_sensor.temperature
    << '\n';

  const bool invalid =
    AsciiProtocol::parseSensorLine("TEMP:abc", protocol_sensor);

  std::cout
    << "Invalid TEMP"
    << " | parsed=" << invalid
    << " | temperature=" << protocol_sensor.temperature
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 17] ASCII sensor parser\n";

  SensorState parsed_sensor;

  const char* sensor_lines[] =
  {
    "TEMP:32.5",
    "PRESSURE:75",
    "DOOR:OPEN",
    "ESTOP:ON",
    "MOTOR:FAULT"
  };

  for (const char* line : sensor_lines)
  {
    const bool parsed =
      AsciiProtocol::parseSensorLine(line, parsed_sensor);

    std::cout
      << line
      << " | parsed=" << parsed
      << '\n';
  }

  std::cout
    << "SensorState"
    << " | temperature=" << parsed_sensor.temperature
    << " | pressure=" << parsed_sensor.pressure
    << " | door_closed=" << parsed_sensor.door_closed
    << " | emergency_stop=" << parsed_sensor.emergency_stop
    << " | motor_fault=" << parsed_sensor.motor_fault
    << '\n';

  // 잘못된 입력에 대한 방어 검증
  const bool invalid_pressure =
    AsciiProtocol::parseSensorLine(
      "PRESSURE:abc", parsed_sensor
    );

  const bool invalid_door =
    AsciiProtocol::parseSensorLine(
      "DOOR:UNKNOWN", parsed_sensor
    );

  std::cout
    << "Invalid inputs"
    << " | pressure=" << invalid_pressure
    << " | door=" << invalid_door
    << " | pressure_after=" << parsed_sensor.pressure
    << " | door_after=" << parsed_sensor.door_closed
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 18] ASCII device serializer\n";

  DeviceState serializer_device;

  serializer_device.pump_on = true;
  serializer_device.heater_on = true;
  serializer_device.valve_open = false;
  serializer_device.buzzer_on = false;
  serializer_device.run_led = true;
  serializer_device.error_led = false;

  const std::string serialized =
    AsciiProtocol::serializeDeviceState(serializer_device);

  std::cout << serialized;

//=======================================================//

  std::cout << "\n[TEST 19] ASCII parser boundary checks\n";

  SensorState boundary_sensor;

  const char* invalid_lines[] =
  {
    "",
    "TEMP",
    "TEMP:",
    "TEMP:abc",
    "TEMP:26.5abc",
    "TEMP:nan",
    "TEMP:inf",
    "PRESSURE:abc",
    "PRESSURE:75.5",
    "PRESSURE:999999999999999999999",
    "DOOR:UNKNOWN",
    "ESTOP:INVALID",
    "MOTOR:UNKNOWN",
    "UNKNOWN:123"
  };

  int rejected = 0;

  for (const char* line : invalid_lines)
  {
    const bool parsed =
      AsciiProtocol::parseSensorLine(line, boundary_sensor);

    if (!parsed)
    {
      ++rejected;
    }

    std::cout
      << "Input=[" << line << "]"
      << " | parsed=" << parsed
      << '\n';
  }

  std::cout
    << "Rejected=" << rejected
    << "/14"
    << " | temperature=" << boundary_sensor.temperature
    << " | pressure=" << boundary_sensor.pressure
    << '\n';

//=======================================================//

  return 0;
}
