#include <iostream>

#include "alarm_code.hpp"
#include "command.hpp"
#include "equipment_controller.hpp"
#include "sensor_state.hpp"

int main()
{
  Command start_command;
  start_command.type = CommandType::Start;

  std::cout << "C++ Equipment Controller\n";

  std::cout << "\n[TEST 1] START with door open\n";

  EquipmentController blocked_controller;

  SensorState blocked_sensor;
  blocked_sensor.door_closed = false;

  blocked_controller.setSensorState(blocked_sensor);
  blocked_controller.handleCommand(start_command);

  std::cout
    << "State: "
    << static_cast<int>(blocked_controller.getState())
    << '\n';

  std::cout
    << "Sequence step: "
    << static_cast<int>(blocked_controller.getSequenceStep())
    << '\n';

  std::cout << "\n[TEST 2] Normal sequence\n";

  EquipmentController controller;

  controller.handleCommand(start_command);

  std::cout
    << "After START state: "
    << static_cast<int>(controller.getState())
    << '\n';

  std::cout
    << "Sequence step: "
    << static_cast<int>(controller.getSequenceStep())
    << '\n';

  for (int i = 0; i < 9; ++i)
  {
    controller.update();

    const DeviceState& device = controller.getDeviceState();

    std::cout
      << "Update " << i + 1
      << " | state=" << static_cast<int>(controller.getState())
      << " | step=" << static_cast<int>(controller.getSequenceStep())
      << " | pump=" << device.pump_on
      << " | heater=" << device.heater_on
      << " | run_led=" << device.run_led
      << '\n';
  }

  std::cout << "\n[TEST 3] Door open during RUN\n";

  EquipmentController fault_controller;

  fault_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    fault_controller.update();
  }

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(fault_controller.getState())
    << " | step=" << static_cast<int>(fault_controller.getSequenceStep())
    << '\n';

  SensorState fault_sensor;
  fault_sensor.door_closed = false;

  fault_controller.setSensorState(fault_sensor);
  fault_controller.update();

  const DeviceState& fault_device =
    fault_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(fault_controller.getState())
    << " | step=" << static_cast<int>(fault_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(fault_controller.getAlarmCode())
    << " | pump=" << fault_device.pump_on
    << " | heater=" << fault_device.heater_on
    << " | run_led=" << fault_device.run_led
    << " | error_led=" << fault_device.error_led
    << " | buzzer=" << fault_device.buzzer_on
    << '\n';

  return 0;
}