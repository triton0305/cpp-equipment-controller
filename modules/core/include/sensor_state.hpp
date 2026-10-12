#ifndef SENSOR_STATE_HPP
#define SENSOR_STATE_HPP

enum class SensorKind { Temperature, Humidity, Pressure };

struct SensorState
{
  double temperature = 25.0;
  int pressure = 60;

  // Values are usable as measurements only while the corresponding flag is true.
  double humidity = 0.0;
  bool temperature_valid = false;
  bool humidity_valid = false;
  bool pressure_valid = false;

  bool door_closed = true;
  bool emergency_stop = false;
  bool motor_fault = false;
  bool communication_ok = true;
};

#endif