#include "controller_runtime.hpp"
#include "ascii_protocol.hpp"
#include <iostream>
#include <utility>
#include <vector>
#include <iomanip>
#include <sstream>

namespace {
std::string measurement(double value)
{
  std::ostringstream text;
  text << std::fixed << std::setprecision(1) << value;
  return text.str();
}
}

ControllerRuntime::ControllerRuntime(std::string device, EquipmentController& controller, Now now)
  : device_(std::move(device)), controller_(controller), now_(std::move(now)),
    sensor_(controller.getSensorState())
{
  sensor_.temperature_valid = sensor_.humidity_valid = sensor_.pressure_valid = false;
  sensor_.communication_ok = false;
  ready_timer_active_ = false;
  ready_indicator_on_ = false;
  door_status_received_ = false;
  estop_status_received_ = false;
  controller_.setSensorState(sensor_);
  next_reconnect_ = now_();
  next_status_ = now_();
}

void ControllerRuntime::communicationFault(const char* reason)
{
  sensor_.communication_ok = false;
  sensor_.temperature_valid = sensor_.humidity_valid = sensor_.pressure_valid = false;
  if (rearm_ != Rearm::Armed) rearm_ = Rearm::WaitOpen;

  ready_timer_active_ = false;
  ready_indicator_on_ = false;
  door_status_received_ = false;
  estop_status_received_ = false;

  controller_.setSensorState(sensor_);
  updateController();
  std::cerr << reason << "; communication invalid, state="
            << static_cast<int>(controller_.getState()) << " alarm="
            << static_cast<int>(controller_.getAlarmCode()) << '\n';
}

void ControllerRuntime::disconnect()
{
  serial_.closePort();
  std::cerr << "EVENT UART disconnect\n";
  next_reconnect_ = now_() + ReconnectInterval;
}

bool ControllerRuntime::sendOutputs(bool force_safe, bool resynchronize)
{
  force_safe = force_safe || !sensor_.communication_ok;
  DeviceState output = controller_.getDeviceState();
  if (output.buzzer_on)
  {
    if (!buzzer_timer_active_ && !buzzer_expired_)
    {
      buzzer_started_ = now_();
      buzzer_timer_active_ = true;
    }

    if (buzzer_timer_active_ &&
        now_() - buzzer_started_ >= BuzzerDuration)
    {
      buzzer_timer_active_ = false;
      buzzer_expired_ = true;
    }

    if (buzzer_expired_)
    {
      output.buzzer_on = false;
    }
  }
  else
  {
    buzzer_timer_active_ = false;
    buzzer_expired_ = false;
  }
  if (force_safe)
  {
    output.pump_on = output.heater_on = output.run_led = false;
    output.valve_open = false;
  }
  // Terminate an old fragmented MCU RX line before a safe six-line refresh.
  std::string state_command = "STATE:SAFE\n";

  if (!force_safe)
  {
    switch (controller_.getState())
    {
      case EquipmentState::Idle:
        state_command = ready_indicator_on_
            ? "STATE:READY\n"
            : "STATE:IDLE\n";
        break;

      case EquipmentState::Ready:
        state_command = "STATE:READY\n";
        break;

      case EquipmentState::Run:
        state_command = "STATE:RUN\n";
        break;

      case EquipmentState::Error:
        state_command = "STATE:ERROR\n";
        break;
    }
  }

  const std::string packet =
      (resynchronize ? "\n" : "") +
      AsciiProtocol::serializeDeviceState(output) +
      state_command;
  if (serial_.writeAll(packet)) return true;
  communicationFault("UART write failed");
  disconnect();
  return false;
}

bool ControllerRuntime::checkSensorTimeout()
{
  if (now_() - last_sensor_ < SensorTimeout) return false;
  communicationFault("Sensor stale");
  // A silent RX can still have a working TX: attempt the safe snapshot first.
  sendOutputs(true);
  disconnect();
  return true;
}

void ControllerRuntime::updateController(bool advance)
{
  const auto previous = controller_.getState();
  const bool completed = run_timer_active_ && now_() - run_started_ >= RunDuration;
  // Each RX line still checks safety, but at most one sequence update per poll.
  if ((advance || completed) && !sequence_advanced_)
  {
    controller_.update(completed);
    sequence_advanced_ = true;
  }
  else controller_.checkSafety();
  if (previous == EquipmentState::Run && controller_.getState() == EquipmentState::Idle && completed)
  {
    rearm_ = Rearm::WaitOpen;
    ready_timer_active_ = ready_indicator_on_ = false;
  }
  if (controller_.getState() == EquipmentState::Run && !run_timer_active_)
  {
    run_started_ = now_();
    run_timer_active_ = true;
  }
  if (controller_.getState() != EquipmentState::Run) run_timer_active_ = false;
  if (controller_.getState() != reported_state_)
  {
    reported_state_ = controller_.getState();
    std::cerr << "EVENT Controller state=" << static_cast<int>(reported_state_)
              << " alarm=" << static_cast<int>(controller_.getAlarmCode()) << '\n';
  }
}

bool ControllerRuntime::updateReady(Clock::time_point now)
{
  const bool previous = ready_indicator_on_;
  const bool safe = serial_.isOpen() && sensor_.communication_ok &&
      door_status_received_ && estop_status_received_ && sensor_.door_closed &&
      !sensor_.emergency_stop && controller_.getState() == EquipmentState::Idle &&
      controller_.getSequenceStep() == SequenceStep::None && rearm_ == Rearm::Armed;
  if (!safe)
  {
    ready_timer_active_ = ready_indicator_on_ = false;
  }
  else if (!ready_timer_active_ && !ready_indicator_on_)
  {
    ready_started_ = now;
    ready_timer_active_ = true;
  }
  if (ready_timer_active_ && now - ready_started_ >= ReadyDelay)
  {
    ready_timer_active_ = false;
    ready_indicator_on_ = true;
  }
  return previous != ready_indicator_on_;
}

bool ControllerRuntime::handleCommand(const Command& command)
{
  if (!serial_.isOpen() || checkSensorTimeout()) return false;
  expireEnvironment();
  updateReady(now_());
  if (command.type == CommandType::Start && !ready_indicator_on_) return false;
  if (command.type == CommandType::Reset &&
      (controller_.getState() != EquipmentState::Error ||
       !door_status_received_ || !estop_status_received_)) return false;
  const auto before = controller_.getState();
  controller_.handleCommand(command);
  if (controller_.getState() == before) return false;
  if (command.type == CommandType::Reset) rearm_ = Rearm::Armed;
  if (controller_.getState() != EquipmentState::Run) run_timer_active_ = false;
  std::cerr << "EVENT command=" << static_cast<int>(command.type)
            << " state=" << static_cast<int>(controller_.getState()) << '\n';
  // START/STOP/RESET always discard qualification accumulated for a previous cycle.
  ready_timer_active_ = ready_indicator_on_ = false;
  return sendOutputs(!sensor_.communication_ok);
}

void ControllerRuntime::poll()
{
  sequence_advanced_ = false;
  expireEnvironment();
  service();
  expireEnvironment();
  logStatus();
}

void ControllerRuntime::service()
{
  if (!serial_.isOpen())
  {
    if (now_() < next_reconnect_) return;
    if (!serial_.openPort(device_))
    {
      communicationFault("UART open failed");
      disconnect();
      return;
    }
    std::cerr << "EVENT UART connected/reconnected; awaiting fresh sensors\n";
    // Opening the port neither validates sensors nor clears the Core error.
    sensor_.communication_ok = false;
    controller_.setSensorState(sensor_);
    last_sensor_ = now_(); // First valid line must arrive within this deadline.
    if (!sendOutputs(true, true)) return;
    next_safe_refresh_ = now_() + SafeRefreshInterval;
    return;
  }

  if (checkSensorTimeout()) return;

  if (serial_.isOpen() &&
      buzzer_timer_active_ &&
      now_() - buzzer_started_ >= BuzzerDuration)
  {
    if (!sendOutputs(false)) return;
  }
  std::vector<std::string> lines;
  if (!serial_.readLines(lines))
  {
    communicationFault("UART read failed");
    disconnect();
    return;
  }
  const auto received_at = now_();
  bool command_seen = false;
  for (const auto& line : lines)
  {
    // A blocked write must not make subsequent lines from the same old RX batch
    // look freshly received. Expire before accepting data at the deadline.
    if (checkSensorTimeout()) return;
    const auto command = parseCommand(line);
    if (command.type != CommandType::Invalid)
    {
      command_seen = true;
      handleCommand(command);
      if (!serial_.isOpen()) return;
      continue; // Commands never refresh the sensor deadline.
    }
    const bool temperature_line = line.rfind("TEMP:", 0) == 0;
    const bool humidity_line = line.rfind("HUMIDITY:", 0) == 0;
    const bool pressure_line = line.rfind("PRESSURE:", 0) == 0;
    if (!AsciiProtocol::parseSensorLine(line, sensor_))
    {
      controller_.setSensorState(sensor_); // Malformed measurement invalidates its flag.
      std::cerr << "Invalid SENSOR line: " << line << '\n';
      continue;
    }

    if ((temperature_line && !sensor_.temperature_valid) ||
        (humidity_line && !sensor_.humidity_valid) ||
        (pressure_line && !sensor_.pressure_valid))
    {
      controller_.setSensorState(sensor_);
      std::cerr << "EVENT invalid measurement: " << line << '\n';
      continue; // Failed measurements cannot refresh the communication deadline.
    }
    if (temperature_line) last_temperature_ = received_at;
    if (humidity_line) last_humidity_ = received_at;
    if (pressure_line) last_pressure_ = received_at;
    if (line.rfind("DOOR:", 0) == 0)
    {
      door_status_received_ = true;
      if (controller_.getState() == EquipmentState::Idle)
      {
        if (rearm_ == Rearm::WaitOpen && !sensor_.door_closed) rearm_ = Rearm::WaitClose;
        else if (rearm_ == Rearm::WaitClose && sensor_.door_closed) rearm_ = Rearm::Armed;
      }

      if (!sensor_.door_closed)
      {
        ready_timer_active_ = false;
        ready_indicator_on_ = false;
      }
    }
    else if (line.rfind("ESTOP:", 0) == 0)
    {
      estop_status_received_ = true;

      if (sensor_.emergency_stop)
      {
        ready_timer_active_ = false;
        ready_indicator_on_ = false;
      }
    }

    if (!sensor_.communication_ok) std::cerr << "EVENT sensor communication recovered\n";
    last_sensor_ = received_at;
    sensor_.communication_ok = true;
    controller_.setSensorState(sensor_);
    if (temperature_line) controller_.observeMeasurement(SensorKind::Temperature);
    if (humidity_line) controller_.observeMeasurement(SensorKind::Humidity);
    if (pressure_line) controller_.observeMeasurement(SensorKind::Pressure);
    updateController();

    updateReady(received_at);

    if (!sendOutputs(false)) return; // Never replay the rest of a failed RX batch.

  }

  if (checkSensorTimeout()) return;

  // Full RX batches and command input cannot compress the preparation sequence.
  expireEnvironment();
  if (sensor_.communication_ok)
  {
    const auto step = controller_.getSequenceStep();
    updateController(!command_seen);
    if (step != controller_.getSequenceStep() && !sendOutputs(false)) return;
  }
  if (updateReady(now_()) && !sendOutputs(false)) return;

  if (!sensor_.communication_ok && now_() >= next_safe_refresh_)
  {
    if (!sendOutputs(true)) return;
    next_safe_refresh_ = now_() + SafeRefreshInterval;
  }
}

void ControllerRuntime::expireEnvironment()
{
  if (sensor_.pressure_valid && now_() - last_pressure_ >= SensorTimeout)
  {
    sensor_.pressure_valid = false;
    std::cerr << "EVENT Pressure stale\n";
  }
  if (sensor_.temperature_valid && now_() - last_temperature_ >= EnvironmentTimeout)
  {
    sensor_.temperature_valid = false;
    std::cerr << "EVENT Temperature stale\n";
  }
  if (sensor_.humidity_valid && now_() - last_humidity_ >= EnvironmentTimeout)
  {
    sensor_.humidity_valid = false;
    std::cerr << "EVENT Humidity stale\n";
  }
  controller_.setSensorState(sensor_);
}

void ControllerRuntime::logStatus()
{
  if (now_() < next_status_) return;
  next_status_ = now_() + std::chrono::seconds(1);
  const auto& d = controller_.getDeviceState();
  std::cout << "STATUS Temperature="
            << (sensor_.temperature_valid ? measurement(sensor_.temperature) : "unavailable")
            << " Humidity="
            << (sensor_.humidity_valid ? measurement(sensor_.humidity) : "unavailable")
            << " Pressure="
            << (sensor_.pressure_valid ? std::to_string(sensor_.pressure) : "unavailable")
            << " Door=" << (door_status_received_ ? (sensor_.door_closed ? "CLOSED" : "OPEN") : "unknown")
            << " E-Stop=" << (estop_status_received_ ? (sensor_.emergency_stop ? "ON" : "OFF") : "unknown")
            << " ControllerState=" << static_cast<int>(controller_.getState())
            << " Ready=" << ready_indicator_on_
            << " Rearm=" << static_cast<int>(rearm_)
            << " SequenceStep=" << static_cast<int>(controller_.getSequenceStep())
            << " Alarm=" << static_cast<int>(controller_.getAlarmCode())
            << " Pump=" << d.pump_on << " Heater=" << d.heater_on << '\n';
}

void ControllerRuntime::shutdown()
{
  controller_.handleCommand(Command{CommandType::Stop});
  if (serial_.isOpen()) sendOutputs(true);
  serial_.closePort();
}
