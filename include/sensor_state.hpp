#ifndef SENSOR_STATE_HPP
#define SENSOR_STATE_HPP

struct SensorState
{
  double temperature = 25.0;
  int pressure = 60;

  bool door_closed = true;
  bool emergency_stop = false;
  bool motor_fault = false;
  bool communication_ok = true;
};

#endif