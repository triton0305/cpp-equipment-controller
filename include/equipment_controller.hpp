#ifndef EQUIPMENT_CONTROLLER_HPP
#define EQUIPMENT_CONTROLLER_HPP

#include "alarm_code.hpp"
#include "command.hpp"
#include "device_state.hpp"
#include "equipment_state.hpp"
#include "sensor_state.hpp"

class EquipmentController
{
public:
  EquipmentController();

  void handleCommand(const Command& command);
  void update();

  void setSensorState(const SensorState& sensor_state);

  EquipmentState getState() const;
  SequenceStep getSequenceStep() const;
  AlarmCode getAlarmCode() const;

  const SensorState& getSensorState() const;
  const DeviceState& getDeviceState() const;

private:
  void handleStart();
  void handleReset();
  void advanceSequence();
  void checkInterlocks();
  void enterError(AlarmCode alarm_code);

  EquipmentState state_;
  SequenceStep sequence_step_;
  AlarmCode alarm_code_;

  SensorState sensor_state_;
  DeviceState device_state_;
};

#endif