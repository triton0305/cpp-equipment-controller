#ifndef DEVICE_STATE_HPP
#define DEVICE_STATE_HPP

struct DeviceState
{
  bool pump_on = false;
  bool heater_on = false;
  bool valve_open = false;
  bool buzzer_on = false;

  bool run_led = false;
  bool error_led = false;
};

#endif