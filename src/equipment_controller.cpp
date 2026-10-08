#include "equipment_controller.hpp"
#include "logger.hpp"

#include <string>

namespace
{
  constexpr int kMinimumOperatingPressure = 50;
  constexpr double kMaximumOperatingTemperature = 80.0;
}

EquipmentController::EquipmentController()
  : state_(EquipmentState::Idle),
    sequence_step_(SequenceStep::None),
    alarm_code_(AlarmCode::None)
{
}

void EquipmentController::handleCommand(const Command& command)
{
  switch (command.type)
  {
    case CommandType::Start:
      handleStart();
      break;

    case CommandType::Stop:
      handleStop();
      break;

    case CommandType::Reset:
      handleReset();
      break;

    default:
      break;
  }
}

void EquipmentController::update()
{
  checkInterlocks();

  if (state_ == EquipmentState::Error)
  {
    return;
  }

  advanceSequence();
}

void EquipmentController::setLogger(Logger* logger)
{
  logger_ = logger;
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

AlarmCode EquipmentController::getAlarmCode() const
{
  return alarm_code_;
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

  if (logger_)
  {
    logger_->log(LogLevel::Info, "START accepted: IDLE -> READY");
  }
}

void EquipmentController::handleStop()
{
  if (state_ != EquipmentState::Ready &&
      state_ != EquipmentState::Run)
  {
    return;
  }

  sequence_step_ = SequenceStep::None;

  device_state_.pump_on = false;
  device_state_.heater_on = false;
  device_state_.valve_open = false;

  device_state_.run_led = false;

  state_ = EquipmentState::Idle;

  if (logger_)
  {
    logger_->log(LogLevel::Info, "STOP accepted: -> IDLE");
  }
}

void EquipmentController::handleReset()
{
  if (state_ != EquipmentState::Error)
  {
    return;
  }

  if (!sensor_state_.door_closed ||
      sensor_state_.emergency_stop ||
      sensor_state_.motor_fault ||
      !sensor_state_.communication_ok ||
      sensor_state_.temperature > kMaximumOperatingTemperature ||
      sensor_state_.pressure < kMinimumOperatingPressure)
  {
    return;
  }

  alarm_code_ = AlarmCode::None;
  sequence_step_ = SequenceStep::None;

  device_state_.pump_on = false;
  device_state_.heater_on = false;
  device_state_.valve_open = false;

  device_state_.run_led = false;
  device_state_.error_led = false;
  device_state_.buzzer_on = false;

  state_ = EquipmentState::Idle;

  if(logger_)
  {
    logger_->log(LogLevel::Info, "RESET accepted: ERROR -> IDLE");
  }
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

void EquipmentController::checkInterlocks()
{
  if (state_ != EquipmentState::Ready &&
      state_ != EquipmentState::Run)
  {
    return;
  }

  if(sensor_state_.emergency_stop)
  {
    enterError(AlarmCode::EmergencyStop);
    return;
  }

  if(!sensor_state_.communication_ok)
  {
    enterError(AlarmCode::CommunicationFault);
    return;
  }

  if (!sensor_state_.door_closed)
  {
    enterError(AlarmCode::DoorOpen);
    return;
  }

  if(sensor_state_.motor_fault)
  {
    enterError(AlarmCode::MotorFault);
    return;
  }

  if (sensor_state_.temperature > kMaximumOperatingTemperature)
  {
    enterError(AlarmCode::OverTemperature);
    return;
  }

  if (device_state_.pump_on &&
      sensor_state_.pressure < kMinimumOperatingPressure)
  {
    enterError(AlarmCode::PressureFault);
  }
}

void EquipmentController::enterError(AlarmCode alarm_code)
{
  alarm_code_ = alarm_code;

  sequence_step_ = SequenceStep::None;

  device_state_.pump_on = false;
  device_state_.heater_on = false;
  device_state_.valve_open = false;

  device_state_.run_led = false;
  device_state_.error_led = true;
  device_state_.buzzer_on = true;

  state_ = EquipmentState::Error;

  if (logger_)
  {
    logger_->log(
      LogLevel::Error,
      "FAULT detected: alarm=" + std::to_string(static_cast<int>(alarm_code))
    );
  }
}