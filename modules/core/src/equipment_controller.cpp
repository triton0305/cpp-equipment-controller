#include "equipment_controller.hpp"
#include "logger.hpp"

#include <string>
#include <cmath>

namespace
{
  int condition(double value, double low, double high)
  { return value <= low ? -1 : value >= high ? 1 : 0; }
  bool pressureSafe(const SensorState& s)
  { return s.pressure_valid && s.pressure > 200 && s.pressure < 3900; }
  bool temperatureSafe(const SensorState& s)
  { return s.temperature_valid && std::isfinite(s.temperature) && s.temperature > 5.0 && s.temperature < 50.0; }
  bool humiditySafe(const SensorState& s)
  { return s.humidity_valid && std::isfinite(s.humidity) && s.humidity > 15.0 && s.humidity < 90.0; }
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

void EquipmentController::update(bool process_complete)
{
  checkInterlocks();

  if (state_ == EquipmentState::Error)
  {
    return;
  }

  advanceSequence(process_complete);
}

void EquipmentController::setLogger(Logger* logger)
{
  logger_ = logger;
}

void EquipmentController::setSensorState(const SensorState& sensor_state)
{
  sensor_state_ = sensor_state;
  if (!sensor_state_.temperature_valid) critical_[0] = {};
  if (!sensor_state_.humidity_valid) critical_[1] = {};
  if (!sensor_state_.pressure_valid) critical_[2] = {};
  if (!sensor_state_.communication_ok) clearCritical();
}

void EquipmentController::clearCritical()
{
  for (auto& counter : critical_) counter = {};
}

void EquipmentController::checkSafety() { checkInterlocks(); }

void EquipmentController::observeMeasurement(SensorKind kind)
{
  checkInterlocks(); // Physical E-Stop, Door and communication faults have priority.
  const auto index = static_cast<unsigned>(kind);
  auto& counter = critical_[index];
  if (state_ != EquipmentState::Ready && state_ != EquipmentState::Run)
  { counter = {}; return; }
  bool valid = false;
  int current = 0;
  AlarmCode alarm = AlarmCode::None;
  switch (kind)
  {
    case SensorKind::Temperature:
      valid = sensor_state_.temperature_valid && std::isfinite(sensor_state_.temperature);
      current = condition(sensor_state_.temperature, 5.0, 50.0);
      alarm = AlarmCode::TemperatureCritical;
      break;
    case SensorKind::Humidity:
      valid = sensor_state_.humidity_valid && std::isfinite(sensor_state_.humidity);
      current = condition(sensor_state_.humidity, 15.0, 90.0);
      alarm = AlarmCode::HumidityCritical;
      break;
    case SensorKind::Pressure:
      valid = sensor_state_.pressure_valid;
      current = condition(sensor_state_.pressure, 200, 3900);
      alarm = AlarmCode::PressureCritical;
      break;
  }
  if (!valid || current == 0) { counter = {}; return; }
  if (counter.condition != current) counter = {current, 1};
  else ++counter.count;
  if (counter.count >= 2) enterError(alarm);
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
  if (state_ != EquipmentState::Idle || sequence_step_ != SequenceStep::None)
  {
    return;
  }

  if (!sensor_state_.door_closed || sensor_state_.emergency_stop || !sensor_state_.communication_ok)
  {
    return;
  }

  clearCritical();
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
      !temperatureSafe(sensor_state_) || !humiditySafe(sensor_state_) ||
      !pressureSafe(sensor_state_))
  {
    return;
  }

  clearCritical();
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

void EquipmentController::advanceSequence(bool process_complete)
{
  switch (sequence_step_)
  {
    case SequenceStep::None:
      break;

    case SequenceStep::CheckDoor:
      device_state_.pump_on = true;
      sequence_step_ = SequenceStep::PumpOn;
      break;

    case SequenceStep::PumpOn:
      sequence_step_ = SequenceStep::CheckPressure;
      break;

    case SequenceStep::CheckPressure:
      if (!pressureSafe(sensor_state_)) break;
      device_state_.heater_on = true;
      sequence_step_ = SequenceStep::HeaterOn;
      break;

    case SequenceStep::HeaterOn:
      sequence_step_ = SequenceStep::CheckTemperature;
      break;

    case SequenceStep::CheckTemperature:
      if (!temperatureSafe(sensor_state_)) break;
      sequence_step_ = SequenceStep::ProcessStart;
      break;

    case SequenceStep::ProcessStart:
      if (!temperatureSafe(sensor_state_)) { sequence_step_ = SequenceStep::CheckTemperature; break; }
      state_ = EquipmentState::Run;
      device_state_.run_led = true;
      sequence_step_ = SequenceStep::ProcessRunning;
      break;

    case SequenceStep::ProcessRunning:
      if (!process_complete) break;
      // Stop at the deadline; finish bookkeeping one step per later update.
      device_state_.pump_on = device_state_.heater_on = false;
      device_state_.valve_open = device_state_.run_led = false;
      state_ = EquipmentState::Idle;
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
  // Check E-Stop in Idle, Ready, and Run.
  // Preserve an existing latched Error state.
  if (state_ == EquipmentState::Error)
  {
    return;
  }

  if (sensor_state_.emergency_stop)
  {
    enterError(AlarmCode::EmergencyStop);
    return;
  }

  if (state_ == EquipmentState::Idle &&
      !sensor_state_.communication_ok)
  {
    enterError(AlarmCode::CommunicationFault);
    return;
  }

  if (state_ != EquipmentState::Ready &&
      state_ != EquipmentState::Run)
  {
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
