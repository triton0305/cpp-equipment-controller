#ifndef ASCII_PROTOCOL_HPP
#define ASCII_PROTOCOL_HPP

#include <string>

#include "device_state.hpp"
#include "sensor_state.hpp"

class AsciiProtocol
{
public:
  static bool parseSensorLine(
    const std::string& line,
    SensorState& sensor
  );

  static std::string serializeDeviceState(
    const DeviceState& device
  );
};

#endif