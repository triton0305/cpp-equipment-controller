#include "controller_runtime.hpp"
#include "ascii_protocol.hpp"
#include <iostream>
#include <utility>
#include <vector>

ControllerRuntime::ControllerRuntime(std::string device, EquipmentController& controller, Now now)
  : device_(std::move(device)), controller_(controller), now_(std::move(now)),
    sensor_(controller.getSensorState())
{
  sensor_.communication_ok = false;
  controller_.setSensorState(sensor_);
  next_reconnect_ = now_();
}

void ControllerRuntime::communicationFault(const char* reason)
{
  sensor_.communication_ok = false;
  controller_.setSensorState(sensor_);
  controller_.update();
  std::cerr << reason << "; communication invalid, state="
            << static_cast<int>(controller_.getState()) << " alarm="
            << static_cast<int>(controller_.getAlarmCode()) << '\n';
}

void ControllerRuntime::disconnect()
{
  serial_.closePort();
  next_reconnect_ = now_() + ReconnectInterval;
}

bool ControllerRuntime::sendOutputs(bool force_safe, bool resynchronize)
{
  DeviceState output = controller_.getDeviceState();
  if (force_safe)
  {
    output.pump_on = output.heater_on = output.run_led = false;
    output.valve_open = false;
  }
  // Terminate an old fragmented MCU RX line before a safe six-line refresh.
  const std::string packet = (resynchronize ? "\n" : "") +
                            AsciiProtocol::serializeDeviceState(output);
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

void ControllerRuntime::poll()
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
    // Opening the port neither validates sensors nor clears the Core error.
    sensor_.communication_ok = false;
    controller_.setSensorState(sensor_);
    last_sensor_ = now_(); // First valid line must arrive within this deadline.
    if (!sendOutputs(true, true)) return;
    next_safe_refresh_ = now_() + SafeRefreshInterval;
    return;
  }

  if (checkSensorTimeout()) return;

  std::vector<std::string> lines;
  if (!serial_.readLines(lines))
  {
    communicationFault("UART read failed");
    disconnect();
    return;
  }
  const auto received_at = now_();
  for (const auto& line : lines)
  {
    // A blocked write must not make subsequent lines from the same old RX batch
    // look freshly received. Expire before accepting data at the deadline.
    if (checkSensorTimeout()) return;
    if (!AsciiProtocol::parseSensorLine(line, sensor_))
    {
      std::cerr << "Invalid SENSOR line: " << line << '\n';
      continue;
    }
    last_sensor_ = received_at;
    sensor_.communication_ok = true;
    controller_.setSensorState(sensor_);
    controller_.update();
    if (!sendOutputs(false)) return; // Never replay the rest of a failed RX batch.
    std::cout << "SENSOR accepted: " << line << '\n';
    std::cout << "Controller state: " << static_cast<int>(controller_.getState())
              << " | Step: " << static_cast<int>(controller_.getSequenceStep())
              << " | Alarm: " << static_cast<int>(controller_.getAlarmCode()) << '\n';
  }
  if (!sensor_.communication_ok && now_() >= next_safe_refresh_)
  {
    if (!sendOutputs(true)) return;
    next_safe_refresh_ = now_() + SafeRefreshInterval;
  }
}

void ControllerRuntime::shutdown()
{
  controller_.handleCommand(Command{CommandType::Stop});
  if (serial_.isOpen()) sendOutputs(true);
  serial_.closePort();
}
