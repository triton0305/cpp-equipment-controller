#ifndef EQUIPMENT_STATE_HPP
#define EQUIPMENT_STATE_HPP

enum class EquipmentState
{
  Idle,
  Ready,
  Run,
  Error
};

// Retain legacy step numbers for STATUS consumers. CheckDoor, CheckPressure,
// CheckTemperature and ProcessRunning are no longer used for timed progression.
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
  Shutdown,
  HeaterOff,
  PumpOff
};

#endif