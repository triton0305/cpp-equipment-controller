#include "ascii_protocol.hpp"

#include <cmath>
#include <stdexcept>

bool AsciiProtocol::parseSensorLine(
  const std::string& line,
  SensorState& sensor
)
{
  const std::size_t pos = line.find(':');

  if (pos == std::string::npos)
  {
    return false;
  }

  const std::string key = line.substr(0, pos);
  const std::string value = line.substr(pos + 1);

  if (value.empty())
  {
    return false;
  }

  // Temperature
  if (key == "TEMP")
  {
    try
    {
      std::size_t consumed = 0;
      const double temperature = std::stod(value, &consumed);

      if (consumed != value.size() ||
          !std::isfinite(temperature))
      {
        return false;
      }

      sensor.temperature = temperature;
      return true;
    }
    catch (const std::exception&)
    {
      return false;
    }
  }

  // Pressure
  if (key == "PRESSURE")
  {
    try
    {
      std::size_t consumed = 0;
      const int pressure = std::stoi(value, &consumed);

      if (consumed != value.size())
      {
        return false;
      }

      sensor.pressure = pressure;
      return true;
    }
    catch (const std::exception&)
    {
      return false;
    }
  }

  // Door
  if (key == "DOOR")
  {
    if (value == "CLOSED")
    {
      sensor.door_closed = true;
      return true;
    }

    if (value == "OPEN")
    {
      sensor.door_closed = false;
      return true;
    }

    return false;
  }

  // Emergency Stop
  if (key == "ESTOP")
  {
    if (value == "ON")
    {
      sensor.emergency_stop = true;
      return true;
    }

    if (value == "OFF")
    {
      sensor.emergency_stop = false;
      return true;
    }

    return false;
  }

  // Motor
  if (key == "MOTOR")
  {
    if (value == "FAULT")
    {
      sensor.motor_fault = true;
      return true;
    }

    if (value == "OK")
    {
      sensor.motor_fault = false;
      return true;
    }

    return false;
  }

  return false;
}

std::string AsciiProtocol::serializeDeviceState(
  const DeviceState& device
)
{
  std::string output;

  output += device.pump_on
    ? "PUMP:ON\n" : "PUMP:OFF\n";

  output += device.heater_on
    ? "HEATER:ON\n" : "HEATER:OFF\n";

  output += device.valve_open
    ? "VALVE:OPEN\n" : "VALVE:CLOSED\n";

  output += device.buzzer_on
    ? "BUZZER:ON\n" : "BUZZER:OFF\n";

  output += device.run_led
    ? "RUN_LED:ON\n" : "RUN_LED:OFF\n";

  output += device.error_led
    ? "ERROR_LED:ON\n" : "ERROR_LED:OFF\n";

  return output;
}