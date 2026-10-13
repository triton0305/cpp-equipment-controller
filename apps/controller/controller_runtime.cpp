#include "controller_runtime.hpp"
#include "uart_diagnostics.hpp"
#include "ascii_protocol.hpp"
#include <iostream>
#include <utility>
#include <vector>
#include <iomanip>
#include <sstream>
#include <tuple>

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

void ControllerRuntime::setDiagnostics(UartDiagnostics* diagnostics)
{
  diagnostics_ = diagnostics;
  serial_.setDiagnostics(diagnostics);
  diagnose("recorder_attached");
}

void ControllerRuntime::diagnosticContext()
{
  if (!diagnostics_) return;
  auto& c = diagnostics_->context;
  c.phase = !serial_.isOpen() ? "Disconnected"
      : link_phase_ == LinkPhase::Synchronizing ? "Synchronizing" : "Operational";
  c.sensor = last_sensor_;
  c.sync = synchronization_started_;
}

void ControllerRuntime::diagnose(const char* event, std::string_view detail)
{
  if (!diagnostics_) return;
  diagnosticContext();
  diagnostics_->record(event, -1, -1, -1, detail);
}

void ControllerRuntime::communicationFault(const char* reason)
{
  diagnose("communication_fault", reason);
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
  diagnose("disconnected");
  std::cerr << "EVENT UART disconnect\n";
  next_reconnect_ = now_() + ReconnectInterval;
}

bool ControllerRuntime::sendOutputs(bool force_safe, bool resynchronize)
{
  diagnosticContext();
  UartDiagnostics::Span trace(diagnostics_, "send_outputs_begin", "send_outputs_end");
  trace.result = 0;
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
  if (serial_.writeAll(packet)) { trace.result = 1; return true; }
  communicationFault("UART write failed");
  disconnect();
  return false;
}

bool ControllerRuntime::checkSensorTimeout()
{
  const bool synchronizing = link_phase_ == LinkPhase::Synchronizing;
  const auto started = synchronizing ? synchronization_started_ : last_sensor_;
  const auto checked_at = now_();
  if (checked_at - started < SensorTimeout) return false;
  if (diagnostics_)
  {
    diagnosticContext();
    diagnostics_->recordAt("timeout_fault", checked_at,
                           synchronizing ? "UART synchronization timeout" : "Sensor stale");
    serial_.diagnosePendingInput();
  }
  communicationFault(synchronizing ? "UART synchronization timeout" : "Sensor stale");
  // A silent RX can still have a working TX: attempt the safe snapshot first.
  sendOutputs(true);
  disconnect();
  return true;
}

void ControllerRuntime::updateController(bool advance)
{
  const auto previous = controller_.getState();
  const auto elapsed = process_timer_active_
      ? std::chrono::duration_cast<std::chrono::milliseconds>(now_() - process_started_)
      : std::chrono::milliseconds::zero();
  const bool completed = process_timer_active_ && elapsed >= EquipmentController::ProcessDuration;
  // Each RX line still checks safety, but at most one sequence update per poll.
  if ((advance || process_timer_active_) && !sequence_advanced_)
  {
    controller_.update(elapsed);
    sequence_advanced_ = true;
  }
  else controller_.checkSafety();
  if (previous == EquipmentState::Run && controller_.getState() == EquipmentState::Idle && completed)
  {
    rearm_ = Rearm::WaitOpen;
    ready_timer_active_ = ready_indicator_on_ = false;
  }
  if (controller_.getState() != EquipmentState::Run) process_timer_active_ = false;
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
  const bool safe = serial_.isOpen() && link_phase_ == LinkPhase::Operational &&
      sensor_.communication_ok && sensor_.temperature_valid && sensor_.pressure_valid &&
      sensor_.temperature >= StartTemperatureMin && sensor_.temperature <= StartTemperatureMax &&
      sensor_.pressure >= StartPressureMin && sensor_.pressure <= StartPressureMax &&
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
  if (!serial_.isOpen() || checkSensorTimeout() ||
      link_phase_ != LinkPhase::Operational) return false;
  expireEnvironment();
  updateReady(now_());
  if (command.type == CommandType::Start && !ready_indicator_on_) return false;
  if (command.type == CommandType::Reset &&
      (controller_.getState() != EquipmentState::Error ||
       !door_status_received_ || !estop_status_received_)) return false;
  const auto before = controller_.getState();
  const auto command_time = now_();
  controller_.handleCommand(command);
  if (controller_.getState() == before) return false;
  if (command.type == CommandType::Start)
  {
    process_started_ = command_time;
    process_timer_active_ = true;
  }
  if (command.type == CommandType::Reset) rearm_ = Rearm::Armed;
  if (controller_.getState() != EquipmentState::Run) process_timer_active_ = false;
  std::cerr << "EVENT command=" << static_cast<int>(command.type)
            << " state=" << static_cast<int>(controller_.getState()) << '\n';
  // START/STOP/RESET always discard qualification accumulated for a previous cycle.
  ready_timer_active_ = ready_indicator_on_ = false;
  return sendOutputs(!sensor_.communication_ok);
}

void ControllerRuntime::poll()
{
  diagnosticContext();
  if (diagnostics_) diagnostics_->beginPoll();
  UartDiagnostics::Span trace(diagnostics_, "poll_begin", "poll_end");
  sequence_advanced_ = false;
  expireEnvironment();
  service();
  expireEnvironment();
  logStatus();
}

void ControllerRuntime::service()
{
  diagnosticContext();
  UartDiagnostics::Span trace(diagnostics_, "service_begin", "service_end");
  if (!serial_.isOpen())
  {
    if (now_() < next_reconnect_) return;
    diagnose("open_begin");
    const bool opened = serial_.openPort(device_);
    diagnose("open_end");
    if (!opened)
    {
      communicationFault("UART open failed");
      disconnect();
      return;
    }
    std::cerr << "EVENT UART connected/reconnected; awaiting fresh sensors\n";
    // Opening the port neither validates sensors nor clears the Core error.
    sensor_.communication_ok = false;
    controller_.setSensorState(sensor_);
    link_phase_ = LinkPhase::Synchronizing;
    synchronization_started_ = now_();
    if (diagnostics_)
    {
      ++diagnostics_->context.session;
      diagnostics_->context.sync_seen = true;
      diagnostics_->context.recovered_seen = false;
    }
    diagnose("sync_begin");
    awaiting_estop_ = false;
    door_status_received_ = estop_status_received_ = false;
    ready_timer_active_ = ready_indicator_on_ = false;
    if (!sendOutputs(true, true)) return;
    next_safe_refresh_ = now_() + SafeRefreshInterval;
    return;
  }

  if (checkSensorTimeout()) return;
  // Track only this service's transmissions: each new sensor batch still
  // refreshes MCU command liveness even when its outputs are unchanged.
  const auto output_state = [&] {
    const auto& d = controller_.getDeviceState();
    return std::make_tuple(controller_.getState(), ready_indicator_on_,
        sensor_.communication_ok, d.pump_on, d.heater_on, d.valve_open,
        d.buzzer_on, d.run_led, d.error_led);
  };
  auto sent_output = output_state();
  bool output_sent = false;
  const auto remember_output = [&] {
    sent_output = output_state();
    output_sent = true;
  };
  // Expiry at poll entry must reset qualification before fresh RX can restore it.
  bool output_refresh_needed = updateReady(now_());

  if (serial_.isOpen() &&
      buzzer_timer_active_ &&
      now_() - buzzer_started_ >= BuzzerDuration)
  {
    if (!sendOutputs(false)) return;
    remember_output();
  }
  std::vector<SerialReadEvent> events;
  if (!serial_.readEvents(events))
  {
    communicationFault("UART read failed");
    disconnect();
    return;
  }
  const auto received_at = now_();
  diagnose("batch_received");
  bool command_seen = false;
  for (const auto& event : events)
  {
    if (diagnostics_) ++diagnostics_->context.rx_index;
    diagnose(event.type == SerialReadEventType::Line ? "rx_event" : "rx_oversized", event.line);
    // A blocked write must not make subsequent lines from the same old RX batch
    // look freshly received. Expire before accepting data at the deadline.
    if (checkSensorTimeout()) return;
    if (event.type == SerialReadEventType::OversizedLineDiscarded)
    {
      awaiting_estop_ = false;
      continue; // Framing discontinuities never refresh either deadline.
    }
    const auto& line = event.line;
    if (link_phase_ == LinkPhase::Synchronizing)
    {
      // Only adjacent parsed DOOR -> ESTOP fields establish a new session.
      // Keep partial candidates out of the operational SensorState.
      SensorState candidate = sensor_;
      const bool valid = AsciiProtocol::parseSensorLine(line, candidate);
      if (valid && line.rfind("DOOR:", 0) == 0)
      {
        diagnose("door_received", line);
        candidate_door_closed_ = candidate.door_closed;
        awaiting_estop_ = true;
        continue;
      }
      const bool synchronized = awaiting_estop_ && valid &&
          line.rfind("ESTOP:", 0) == 0;
      awaiting_estop_ = false;
      if (!synchronized) continue;

      sensor_.door_closed = candidate_door_closed_;
      sensor_.emergency_stop = candidate.emergency_stop;
      door_status_received_ = estop_status_received_ = true;
      diagnose("sync_estop_received", line);
      link_phase_ = LinkPhase::Operational;
      diagnose("sync_complete");
      // The completing ESTOP follows the normal safety/output path below.
    }
    const auto command = parseCommand(line);
    if (command.type != CommandType::Invalid)
    {
      command_seen = true;
      if (handleCommand(command)) remember_output();
      if (!serial_.isOpen()) return;
      continue; // Commands never refresh the sensor deadline.
    }
    const bool temperature_line = line.rfind("TEMP:", 0) == 0;
    const bool humidity_line = line.rfind("HUMIDITY:", 0) == 0;
    const bool pressure_line = line.rfind("PRESSURE:", 0) == 0;
    if (!AsciiProtocol::parseSensorLine(line, sensor_))
    {
      controller_.setSensorState(sensor_); // Malformed measurement invalidates its flag.
      if (updateReady(now_())) output_refresh_needed = true;
      diagnose("invalid_sensor", line);
      std::cerr << "Invalid SENSOR line: " << line << '\n';
      continue;
    }

    if ((temperature_line && !sensor_.temperature_valid) ||
        (humidity_line && !sensor_.humidity_valid) ||
        (pressure_line && !sensor_.pressure_valid))
    {
      controller_.setSensorState(sensor_);
      if (updateReady(now_())) output_refresh_needed = true;
      std::cerr << "EVENT invalid measurement: " << line << '\n';
      continue; // Failed measurements cannot refresh the communication deadline.
    }
    if (temperature_line) last_temperature_ = received_at;
    if (humidity_line) last_humidity_ = received_at;
    if (pressure_line) last_pressure_ = received_at;
    if (line.rfind("DOOR:", 0) == 0)
    {
      diagnose("door_received", line);
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
      diagnose("estop_received", line);
      estop_status_received_ = true;

      if (sensor_.emergency_stop)
      {
        ready_timer_active_ = false;
        ready_indicator_on_ = false;
      }
    }

    if (!sensor_.communication_ok)
    {
      if (diagnostics_)
      {
        diagnostics_->context.recovered = now_();
        diagnostics_->context.recovered_seen = true;
      }
      diagnose("recovered_log_begin");
      std::cerr << "EVENT sensor communication recovered\n";
      diagnose("recovered_log_end");
    }
    last_sensor_ = received_at;
    if (diagnostics_) diagnostics_->context.sensor_seen = true;
    diagnose("last_sensor_updated", line);
    sensor_.communication_ok = true;
    controller_.setSensorState(sensor_);
    const auto previous_state = controller_.getState();
    if (temperature_line) controller_.observeMeasurement(SensorKind::Temperature);
    if (humidity_line) controller_.observeMeasurement(SensorKind::Humidity);
    if (pressure_line) controller_.observeMeasurement(SensorKind::Pressure);
    updateController();

    updateReady(received_at);

    output_refresh_needed = true;
    if (previous_state != EquipmentState::Error &&
        controller_.getState() == EquipmentState::Error)
    {
      // Interlocks and critical measurements must reach the MCU before the
      // next RX event, regardless of any normal refresh waiting in this batch.
      if (!sendOutputs(false)) return;
      remember_output();
    }

  }

  if (checkSensorTimeout()) return;

  // Sensor safety is checked before advancing the elapsed-time process.
  expireEnvironment();
  if (sensor_.communication_ok)
  {
    const auto step = controller_.getSequenceStep();
    updateController(!command_seen);
    if (step != controller_.getSequenceStep()) output_refresh_needed = true;
  }
  if (updateReady(now_())) output_refresh_needed = true;
  if (output_refresh_needed && (!output_sent || sent_output != output_state()))
  {
    if (!sendOutputs(false)) return;
    // A single blocked refresh can still exceed the unchanged RX deadline.
    if (checkSensorTimeout()) return;
  }

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
  diagnosticContext();
  UartDiagnostics::Span trace(diagnostics_, "status_begin", "status_end");
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
  diagnose("shutdown_complete");
}

std::string ControllerRuntime::statusSnapshot() const
{
  const char* states[] = {"Idle", "Ready", "Run", "Error"};
  const auto& d = controller_.getDeviceState();
  std::ostringstream out;
  out << "State=" << states[static_cast<int>(controller_.getState())]
      << " Ready=" << ready_indicator_on_ << " Armed=" << (rearm_ == Rearm::Armed)
      << " Rearm=" << static_cast<int>(rearm_)
      << " Alarm=" << static_cast<int>(controller_.getAlarmCode())
      << " SequenceStep=" << static_cast<int>(controller_.getSequenceStep())
      << " Operational=" << (link_phase_ == LinkPhase::Operational)
      << " CommunicationOK=" << sensor_.communication_ok
      << " Door=" << (door_status_received_ ? (sensor_.door_closed ? "CLOSED" : "OPEN") : "unknown")
      << " EStop=" << (estop_status_received_ ? (sensor_.emergency_stop ? "ON" : "OFF") : "unknown")
      << " Pressure=" << (sensor_.pressure_valid ? std::to_string(sensor_.pressure) : "unavailable")
      << " Temperature=" << (sensor_.temperature_valid ? measurement(sensor_.temperature) : "unavailable")
      << " Humidity=" << (sensor_.humidity_valid ? measurement(sensor_.humidity) : "unavailable")
      << " Pump=" << (d.pump_on ? "ON" : "OFF")
      << " Heater=" << (d.heater_on ? "ON" : "OFF");
  return out.str();
}

std::string ControllerRuntime::handleControlRequest(const std::string& text)
{
  if (text == "status") return statusSnapshot();
  const auto command = parseCommand(text);
  if (command.type == CommandType::Invalid) return "ERROR: invalid command";
  return handleCommand(command) ? "OK" : "REJECTED: conditions not met, no state change, or output failure; use status";
}
