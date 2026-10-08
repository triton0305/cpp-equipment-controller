#ifndef EQUIPMENT_STATE_HPP
#define EQUIPMENT_STATE_HPP

enum class EquipmentState
{
  Idle,
  Ready,
  Run,
  Error
};

enum class SequenceStep
{
  None,
  CheckDoor,
  PumpOn,
  CheckPressure,
  HeaterOn,
  CheckTemperature,
  ProcessStart,
  ProcessRunning,
  ProcessComplete,
  Shutdown
};

#endif