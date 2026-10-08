#include "equipment_controller.hpp"

EquipmentController::EquipmentController()
  : state_(EquipmentState::Idle),
    sequence_step_(SequenceStep::None)
{
}

void EquipmentController::handleCommand(const Command& command)
{
  switch (command.type)
  {
    case CommandType::Start:
      handleStart();
      break;

    default:
      break;
  }
}

void EquipmentController::update()
{
  advanceSequence();
}

void EquipmentController::setSensorState(const SensorState& sensor_state)
{
  sensor_state_ = sensor_state;
}

EquipmentState EquipmentController::getState() const
{
  return state_;
}

SequenceStep EquipmentController::getSequenceStep() const
{
  return sequence_step_;
}

const SensorState& EquipmentController::getSensorState() const
{
  return sensor_state_;
}

const DeviceState& EquipmentController::getDeviceState() const
{
  return device_state_;
}

void EquipmentController::handleStart()
{
  if (state_ != EquipmentState::Idle)
  {
    return;
  }

  if (!sensor_state_.door_closed)
  {
    return;
  }

  state_ = EquipmentState::Ready;
  sequence_step_ = SequenceStep::CheckDoor;
}

void EquipmentController::advanceSequence()
{
  switch (sequence_step_)
  {
    case SequenceStep::None:
      break;

    case SequenceStep::CheckDoor:
      sequence_step_ = SequenceStep::PumpOn;
      break;

    case SequenceStep::PumpOn:
      device_state_.pump_on = true;
      sequence_step_ = SequenceStep::CheckPressure;
      break;

    case SequenceStep::CheckPressure:
      sequence_step_ = SequenceStep::HeaterOn;
      break;

    case SequenceStep::HeaterOn:
      device_state_.heater_on = true;
      sequence_step_ = SequenceStep::CheckTemperature;
      break;

    case SequenceStep::CheckTemperature:
      sequence_step_ = SequenceStep::ProcessStart;
      break;

    case SequenceStep::ProcessStart:
      state_ = EquipmentState::Run;
      device_state_.run_led = true;
      sequence_step_ = SequenceStep::ProcessRunning;
      break;

    case SequenceStep::ProcessRunning:
      sequence_step_ = SequenceStep::ProcessComplete;
      break;

    case SequenceStep::ProcessComplete:
      sequence_step_ = SequenceStep::Shutdown;
      break;

    case SequenceStep::Shutdown:
      device_state_.pump_on = false;
      device_state_.heater_on = false;
      device_state_.valve_open = false;
      device_state_.run_led = false;

      state_ = EquipmentState::Idle;
      sequence_step_ = SequenceStep::None;
      break;
  }
}