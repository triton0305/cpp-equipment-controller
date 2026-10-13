#ifndef EQUIPMENT_CONTROLLER_HPP
#define EQUIPMENT_CONTROLLER_HPP

#include "alarm_code.hpp"
#include "command.hpp"
#include "device_state.hpp"
#include "equipment_state.hpp"
#include "sensor_state.hpp"
#include <chrono>

class Logger;

class EquipmentController
{
public:
  EquipmentController();

  void handleCommand(const Command& command);
  // Runtime supplies elapsed time since an accepted START, using its injected clock.
  static constexpr auto ProcessStageInterval = std::chrono::seconds(6);
  static constexpr auto ProcessDuration = 5 * ProcessStageInterval;
  void update(std::chrono::milliseconds process_elapsed = std::chrono::milliseconds::zero());

  // These paths never advance the process. Observe exactly once per received measurement.
  void checkSafety();
  void observeMeasurement(SensorKind kind);
  void setSensorState(const SensorState& sensor_state);
  void setLogger(Logger* logger);

  EquipmentState getState() const;
  SequenceStep getSequenceStep() const;
  AlarmCode getAlarmCode() const;

  const SensorState& getSensorState() const;
  const DeviceState& getDeviceState() const;

private:
  struct CriticalCounter { int condition = 0; unsigned count = 0; };
  CriticalCounter critical_[3]{};
  void clearCritical();
  void handleStart();
  void handleStop();
  void handleReset();
  void advanceSequence(std::chrono::milliseconds elapsed);
  void logProcess(const char* event);
  void checkInterlocks();
  void enterError(AlarmCode alarm_code);

  EquipmentState state_;
  SequenceStep sequence_step_;
  AlarmCode alarm_code_;

  SensorState sensor_state_;
  DeviceState device_state_;

  Logger* logger_ = nullptr;
};

#endif