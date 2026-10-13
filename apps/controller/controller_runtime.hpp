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
  bool handleCommand(const Command& command);

private:
  void service();
  void logStatus();
  Clock::time_point next_status_{};
  static constexpr auto EnvironmentTimeout = std::chrono::seconds(5);
  Clock::time_point last_temperature_{}, last_humidity_{}, last_pressure_{};
  void expireEnvironment();
  enum class Rearm { Armed, WaitOpen, WaitClose };
  Rearm rearm_ = Rearm::Armed;
  bool sequence_advanced_ = false;
  EquipmentState reported_state_ = EquipmentState::Idle;
  void updateController(bool advance = false);
  static constexpr auto RunDuration = std::chrono::seconds(30);
  Clock::time_point run_started_{};
  bool run_timer_active_ = false;
  bool updateReady(Clock::time_point now);
  void communicationFault(const char* reason);
  void disconnect();
  bool checkSensorTimeout();
  bool sendOutputs(bool force_safe, bool resynchronize = false);

  std::string device_;
  EquipmentController& controller_;
  Now now_;
  SerialTransport serial_;
  SensorState sensor_;

  enum class LinkPhase { Synchronizing, Operational };
  LinkPhase link_phase_ = LinkPhase::Synchronizing;
  Clock::time_point synchronization_started_{};
  bool awaiting_estop_ = false;
  bool candidate_door_closed_ = false;

  Clock::time_point last_sensor_, next_reconnect_, next_safe_refresh_;
  // E-Stop Buzzer
  static constexpr auto BuzzerDuration = std::chrono::seconds(2);
  Clock::time_point buzzer_started_{};
  bool buzzer_timer_active_ = false;
  bool buzzer_expired_ = false;

  // Door safety verification and READY indicator
  static constexpr auto ReadyDelay = std::chrono::seconds(5);
  Clock::time_point ready_started_{};
  bool ready_timer_active_ = false;
  bool ready_indicator_on_ = false;

  // Require fresh Door and E-Stop readings after each connection.
  bool door_status_received_ = false;
  bool estop_status_received_ = false;
};
#endif
