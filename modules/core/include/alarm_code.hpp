#ifndef ALARM_CODE_HPP
#define ALARM_CODE_HPP

enum class AlarmCode
{
  None,
  DoorOpen,
  PressureFault,
  OverTemperature,
  MotorFault,
  EmergencyStop,
  CommunicationFault
};

#endif