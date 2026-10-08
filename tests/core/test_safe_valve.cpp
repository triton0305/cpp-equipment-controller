#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#include "equipment_controller.hpp"

namespace
{
  void require(bool condition, const std::string& message)
  {
    if (!condition)
    {
      throw std::runtime_error(message);
    }
  }

  struct FaultCase
  {
    const char* name;
    AlarmCode alarm;
    int updatesBeforeFault;
    void (*inject)(SensorState&);
  };
}

int main()
{
  try
  {
    // Reuse TEST 3-8 inputs. Only the omitted valve field is under test here.
    const std::array<FaultCase, 6> cases =
    {{
      {"Door", AlarmCode::DoorOpen, 6, [](SensorState& sensor)
      {
        sensor.door_closed = false;
      }},
      {"Pressure", AlarmCode::PressureFault, 2, [](SensorState& sensor)
      {
        sensor.pressure = 40;
      }},
      {"Temperature", AlarmCode::OverTemperature, 4, [](SensorState& sensor)
      {
        sensor.temperature = 90.0;
      }},
      {"Motor", AlarmCode::MotorFault, 6, [](SensorState& sensor)
      {
        sensor.motor_fault = true;
      }},
      {"Emergency stop", AlarmCode::EmergencyStop, 6, [](SensorState& sensor)
      {
        sensor.emergency_stop = true;
      }},
      {"Communication", AlarmCode::CommunicationFault, 6, [](SensorState& sensor)
      {
        sensor.communication_ok = false;
      }}
    }};

    for (const auto& test : cases)
    {
      EquipmentController controller;
      controller.handleCommand(Command{CommandType::Start});
      for (int i = 0; i < test.updatesBeforeFault; ++i)
      {
        controller.update();
      }

      SensorState sensor;
      test.inject(sensor);
      controller.setSensorState(sensor);
      controller.update();

      const std::string context(test.name);
      require(controller.getState() == EquipmentState::Error &&
              controller.getAlarmCode() == test.alarm,
              context + ": fault scenario was not reached");
      // Public API exposes no way to open the valve: this checks the resulting
      // safe position, not an open-to-closed transition.
      require(!controller.getDeviceState().valve_open,
              context + ": valve must be closed after fault");

      if (test.alarm == AlarmCode::EmergencyStop)
      {
        controller.handleCommand(Command{CommandType::Reset});
        require(controller.getState() == EquipmentState::Error,
                "Faulted RESET scenario was not reached");
        require(!controller.getDeviceState().valve_open,
                "Valve must be closed after rejected RESET");

        controller.setSensorState(SensorState{});
        controller.handleCommand(Command{CommandType::Reset});
        require(controller.getState() == EquipmentState::Idle &&
                controller.getAlarmCode() == AlarmCode::None,
                "Recovered RESET scenario was not reached");
        require(!controller.getDeviceState().valve_open,
                "Valve must be closed after recovery RESET");
        controller.update();
        require(!controller.getDeviceState().valve_open,
                "Valve must remain closed after RESET + update");
      }
    }

    std::cout << "[TEST 30] Safe valve PASS: six faults, rejected RESET, "
              << "recovery RESET and subsequent update\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "[TEST 30] FAIL: " << error.what() << '\n';
    return 1;
  }
}
