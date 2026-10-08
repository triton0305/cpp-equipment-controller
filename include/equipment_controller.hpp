#ifndef EQUIPMENT_CONTROLLER_HPP
#define EQUIPMENT_CONTROLLER_HPP

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

  const SensorState& getSensorState() const;
  const DeviceState& getDeviceState() const;

private:
  void handleStart();
  void advanceSequence();

  EquipmentState state_;
  SequenceStep sequence_step_;

  SensorState sensor_state_;
  DeviceState device_state_;
};

#endif