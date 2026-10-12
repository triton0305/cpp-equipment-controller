#ifndef CONTROLLER_RUNTIME_HPP
#define CONTROLLER_RUNTIME_HPP

#include "equipment_controller.hpp"
#include "serial_transport.hpp"
#include <chrono>
#include <functional>
#include <string>

// Foreground UART lifecycle only; equipment decisions stay in the existing Core.
class ControllerRuntime
{
public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  static constexpr auto SensorTimeout = std::chrono::milliseconds(1000);
  static constexpr auto ReconnectInterval = std::chrono::milliseconds(500);
  static constexpr auto SafeRefreshInterval = std::chrono::milliseconds(100);

  ControllerRuntime(std::string device, EquipmentController& controller,
                    Now now = Clock::now);
  void poll();
  void shutdown();

private:
  void communicationFault(const char* reason);
  void disconnect();
  bool checkSensorTimeout();
  bool sendOutputs(bool force_safe, bool resynchronize = false);

  std::string device_;
  EquipmentController& controller_;
  Now now_;
  SerialTransport serial_;
  SensorState sensor_;
  Clock::time_point last_sensor_, next_reconnect_, next_safe_refresh_;
};
#endif
