#pragma once
#include "sensor_state.hpp"
// Explicit valid measurements; defaults represent unavailable sensors.
inline SensorState normalSensors()
{
  SensorState s;
  s.temperature = 25.0; s.humidity = 45.0; s.pressure = 2048;
  s.temperature_valid = s.humidity_valid = s.pressure_valid = true;
  return s;
}
