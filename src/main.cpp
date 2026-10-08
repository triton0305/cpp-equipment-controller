#include <iostream>

#include "alarm_code.hpp"
#include "command.hpp"
#include "equipment_controller.hpp"
#include "sensor_state.hpp"
#include "logger.hpp"

int main()
{
  Command start_command;
  start_command.type = CommandType::Start;

  std::cout << "C++ Equipment Controller\n";

//=======================================================//

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

//=======================================================//

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

//=======================================================//

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

//=======================================================//
  
  std::cout << "\n[TEST 4] Pressure fault\n";

  EquipmentController pressure_controller;

  pressure_controller.handleCommand(start_command);

  pressure_controller.update();
  pressure_controller.update();

  const DeviceState& pressure_device_before =
    pressure_controller.getDeviceState();

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(pressure_controller.getState())
    << " | step=" << static_cast<int>(pressure_controller.getSequenceStep())
    << " | pressure=" << pressure_controller.getSensorState().pressure
    << " | pump=" << pressure_device_before.pump_on
    << '\n';

  SensorState pressure_sensor;
  pressure_sensor.pressure = 40;

  pressure_controller.setSensorState(pressure_sensor);
  pressure_controller.update();

  const DeviceState& pressure_device_after =
    pressure_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(pressure_controller.getState())
    << " | step=" << static_cast<int>(pressure_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(pressure_controller.getAlarmCode())
    << " | pressure=" << pressure_controller.getSensorState().pressure
    << " | pump=" << pressure_device_after.pump_on
    << " | heater=" << pressure_device_after.heater_on
    << " | run_led=" << pressure_device_after.run_led
    << " | error_led=" << pressure_device_after.error_led
    << " | buzzer=" << pressure_device_after.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 5] Over temperature\n";

  EquipmentController temperature_controller;

  temperature_controller.handleCommand(start_command);

  for (int i = 0; i < 4; ++i)
  {
    temperature_controller.update();
  }

  const DeviceState& temperature_device_before =
    temperature_controller.getDeviceState();

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(temperature_controller.getState())
    << " | step=" << static_cast<int>(temperature_controller.getSequenceStep())
    << " | temperature=" << temperature_controller.getSensorState().temperature
    << " | pump=" << temperature_device_before.pump_on
    << " | heater=" << temperature_device_before.heater_on
    << '\n';

  SensorState temperature_sensor;
  temperature_sensor.temperature = 90.0;

  temperature_controller.setSensorState(temperature_sensor);
  temperature_controller.update();

  const DeviceState& temperature_device_after =
    temperature_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(temperature_controller.getState())
    << " | step=" << static_cast<int>(temperature_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(temperature_controller.getAlarmCode())
    << " | temperature=" << temperature_controller.getSensorState().temperature
    << " | pump=" << temperature_device_after.pump_on
    << " | heater=" << temperature_device_after.heater_on
    << " | run_led=" << temperature_device_after.run_led
    << " | error_led=" << temperature_device_after.error_led
    << " | buzzer=" << temperature_device_after.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 6] Motor fault during RUN\n";

  EquipmentController motor_controller;

  motor_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    motor_controller.update();
  }

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(motor_controller.getState())
    << " | step=" << static_cast<int>(motor_controller.getSequenceStep())
    << " | motor_fault=" << motor_controller.getSensorState().motor_fault
    << '\n';

  SensorState motor_sensor;
  motor_sensor.motor_fault = true;

  motor_controller.setSensorState(motor_sensor);
  motor_controller.update();

  const DeviceState& motor_device =
    motor_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(motor_controller.getState())
    << " | step=" << static_cast<int>(motor_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(motor_controller.getAlarmCode())
    << " | motor_fault=" << motor_controller.getSensorState().motor_fault
    << " | pump=" << motor_device.pump_on
    << " | heater=" << motor_device.heater_on
    << " | run_led=" << motor_device.run_led
    << " | error_led=" << motor_device.error_led
    << " | buzzer=" << motor_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 7] Emergency stop during RUN\n";

  EquipmentController estop_controller;

  estop_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    estop_controller.update();
  }

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(estop_controller.getState())
    << " | step=" << static_cast<int>(estop_controller.getSequenceStep())
    << " | emergency_stop="
    << estop_controller.getSensorState().emergency_stop
    << '\n';

  SensorState estop_sensor;
  estop_sensor.emergency_stop = true;

  estop_controller.setSensorState(estop_sensor);
  estop_controller.update();

  const DeviceState& estop_device =
    estop_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(estop_controller.getState())
    << " | step=" << static_cast<int>(estop_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(estop_controller.getAlarmCode())
    << " | emergency_stop="
    << estop_controller.getSensorState().emergency_stop
    << " | pump=" << estop_device.pump_on
    << " | heater=" << estop_device.heater_on
    << " | run_led=" << estop_device.run_led
    << " | error_led=" << estop_device.error_led
    << " | buzzer=" << estop_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 8] Communication fault during RUN\n";

  EquipmentController communication_controller;

  communication_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    communication_controller.update();
  }

  std::cout
    << "Before fault"
    << " | state=" << static_cast<int>(communication_controller.getState())
    << " | step=" << static_cast<int>(communication_controller.getSequenceStep())
    << " | communication_ok="
    << communication_controller.getSensorState().communication_ok
    << '\n';

  SensorState communication_sensor;
  communication_sensor.communication_ok = false;

  communication_controller.setSensorState(communication_sensor);
  communication_controller.update();

  const DeviceState& communication_device =
    communication_controller.getDeviceState();

  std::cout
    << "After fault"
    << " | state=" << static_cast<int>(communication_controller.getState())
    << " | step=" << static_cast<int>(communication_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(communication_controller.getAlarmCode())
    << " | communication_ok="
    << communication_controller.getSensorState().communication_ok
    << " | pump=" << communication_device.pump_on
    << " | heater=" << communication_device.heater_on
    << " | run_led=" << communication_device.run_led
    << " | error_led=" << communication_device.error_led
    << " | buzzer=" << communication_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 9] RESET / Recovery\n";

  EquipmentController reset_controller;

  reset_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    reset_controller.update();
  }

  SensorState reset_sensor;
  reset_sensor.emergency_stop = true;

  reset_controller.setSensorState(reset_sensor);
  reset_controller.update();

  Command reset_command;
  reset_command.type = CommandType::Reset;

  // Fault가 유지된 상태에서 RESET 거부
  reset_controller.handleCommand(reset_command);

  std::cout
    << "RESET with fault"
    << " | state=" << static_cast<int>(reset_controller.getState())
    << " | alarm=" << static_cast<int>(reset_controller.getAlarmCode())
    << '\n';

  // Fault 해제 후 RESET 허용
  reset_sensor.emergency_stop = false;
  reset_controller.setSensorState(reset_sensor);

  reset_controller.handleCommand(reset_command);

  const DeviceState& reset_device =
    reset_controller.getDeviceState();

  std::cout
    << "RESET after recovery"
    << " | state=" << static_cast<int>(reset_controller.getState())
    << " | step=" << static_cast<int>(reset_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(reset_controller.getAlarmCode())
    << " | pump=" << reset_device.pump_on
    << " | heater=" << reset_device.heater_on
    << " | run_led=" << reset_device.run_led
    << " | error_led=" << reset_device.error_led
    << " | buzzer=" << reset_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 10] STOP during RUN\n";

  EquipmentController stop_controller;

  stop_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    stop_controller.update();
  }

  const DeviceState& stop_before =
    stop_controller.getDeviceState();

  std::cout
    << "Before STOP"
    << " | state=" << static_cast<int>(stop_controller.getState())
    << " | step=" << static_cast<int>(stop_controller.getSequenceStep())
    << " | pump=" << stop_before.pump_on
    << " | heater=" << stop_before.heater_on
    << '\n';

  Command stop_command;
  stop_command.type = CommandType::Stop;

  stop_controller.handleCommand(stop_command);

  const DeviceState& stop_after =
    stop_controller.getDeviceState();

  std::cout
    << "After STOP"
    << " | state=" << static_cast<int>(stop_controller.getState())
    << " | step=" << static_cast<int>(stop_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(stop_controller.getAlarmCode())
    << " | pump=" << stop_after.pump_on
    << " | heater=" << stop_after.heater_on
    << " | valve=" << stop_after.valve_open
    << " | run_led=" << stop_after.run_led
    << " | error_led=" << stop_after.error_led
    << " | buzzer=" << stop_after.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 11] Logger\n";

  Logger logger("equipment_controller.log");

  logger.log(LogLevel::Info, "Logger initialized");
  logger.log(LogLevel::Warning, "Warning test");
  logger.log(LogLevel::Error, "Error test");

//=======================================================//

  std::cout << "\n[TEST 12] Controller Event Logging\n";

  EquipmentController logging_controller;

  logging_controller.setLogger(&logger);

  // START 이벤트
  logging_controller.handleCommand(start_command);

  // RUN 진입
  for (int i = 0; i < 6; ++i)
  {
    logging_controller.update();
  }

  // STOP 이벤트
  Command stop_logging_command;
  stop_logging_command.type = CommandType::Stop;

  logging_controller.handleCommand(stop_logging_command);

  // 다시 START
  logging_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    logging_controller.update();
  }

  // Emergency Stop 발생
  SensorState logging_sensor;
  logging_sensor.emergency_stop = true;

  logging_controller.setSensorState(logging_sensor);
  logging_controller.update();

  // Fault 해제 후 RESET
  logging_sensor.emergency_stop = false;
  logging_controller.setSensorState(logging_sensor);

  Command reset_logging_command;
  reset_logging_command.type = CommandType::Reset;

  logging_controller.handleCommand(reset_logging_command);

//=======================================================//

  std::cout << "\n[TEST 13] STOP boundary conditions\n";

  Command boundary_stop_command;
  boundary_stop_command.type = CommandType::Stop;

  // IDLE + STOP -> IDLE 유지
  EquipmentController idle_stop_controller;
  idle_stop_controller.handleCommand(boundary_stop_command);

  std::cout
    << "IDLE + STOP"
    << " | state=" << static_cast<int>(idle_stop_controller.getState())
    << " | step=" << static_cast<int>(idle_stop_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(idle_stop_controller.getAlarmCode())
    << '\n';

  // READY + STOP -> IDLE 전환
  EquipmentController ready_stop_controller;

  ready_stop_controller.handleCommand(start_command);
  ready_stop_controller.handleCommand(boundary_stop_command);

  std::cout
    << "READY + STOP"
    << " | state=" << static_cast<int>(ready_stop_controller.getState())
    << " | step=" << static_cast<int>(ready_stop_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(ready_stop_controller.getAlarmCode())
    << '\n';

  // ERROR + STOP -> ERROR 유지
  EquipmentController error_stop_controller;

  error_stop_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    error_stop_controller.update();
  }

  SensorState error_stop_sensor;
  error_stop_sensor.emergency_stop = true;

  error_stop_controller.setSensorState(error_stop_sensor);
  error_stop_controller.update();

  error_stop_controller.handleCommand(boundary_stop_command);

  const DeviceState& error_stop_device =
    error_stop_controller.getDeviceState();

  std::cout
    << "ERROR + STOP"
    << " | state=" << static_cast<int>(error_stop_controller.getState())
    << " | step=" << static_cast<int>(error_stop_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(error_stop_controller.getAlarmCode())
    << " | pump=" << error_stop_device.pump_on
    << " | heater=" << error_stop_device.heater_on
    << " | run_led=" << error_stop_device.run_led
    << " | error_led=" << error_stop_device.error_led
    << " | buzzer=" << error_stop_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 14] RESET boundary conditions\n";

  Command boundary_reset_command;
  boundary_reset_command.type = CommandType::Reset;

  // RUN + RESET -> RUN 유지
  EquipmentController run_reset_controller;

  run_reset_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    run_reset_controller.update();
  }

  run_reset_controller.handleCommand(boundary_reset_command);

  std::cout
    << "RUN + RESET"
    << " | state=" << static_cast<int>(run_reset_controller.getState())
    << " | step=" << static_cast<int>(run_reset_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(run_reset_controller.getAlarmCode())
    << '\n';

  // 정상 RESET 직후 update() -> IDLE 유지
  EquipmentController reset_update_controller;

  reset_update_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    reset_update_controller.update();
  }

  SensorState reset_update_sensor;
  reset_update_sensor.emergency_stop = true;

  reset_update_controller.setSensorState(reset_update_sensor);
  reset_update_controller.update();

  reset_update_sensor.emergency_stop = false;
  reset_update_controller.setSensorState(reset_update_sensor);

  reset_update_controller.handleCommand(boundary_reset_command);
  reset_update_controller.update();

  const DeviceState& reset_update_device =
    reset_update_controller.getDeviceState();

  std::cout
    << "RESET + update"
    << " | state=" << static_cast<int>(reset_update_controller.getState())
    << " | step=" << static_cast<int>(reset_update_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(reset_update_controller.getAlarmCode())
    << " | pump=" << reset_update_device.pump_on
    << " | heater=" << reset_update_device.heater_on
    << " | run_led=" << reset_update_device.run_led
    << " | error_led=" << reset_update_device.error_led
    << " | buzzer=" << reset_update_device.buzzer_on
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 15] Repeated fault stability\n";

  Logger boundary_logger("equipment_controller.log");

  EquipmentController repeated_fault_controller;
  repeated_fault_controller.setLogger(&boundary_logger);

  repeated_fault_controller.handleCommand(start_command);

  for (int i = 0; i < 6; ++i)
  {
    repeated_fault_controller.update();
  }

  SensorState repeated_fault_sensor;
  repeated_fault_sensor.emergency_stop = true;

  repeated_fault_controller.setSensorState(repeated_fault_sensor);

  repeated_fault_controller.update();
  repeated_fault_controller.update();
  repeated_fault_controller.update();

  const DeviceState& repeated_fault_device =
    repeated_fault_controller.getDeviceState();

  std::cout
    << "Repeated fault"
    << " | state=" << static_cast<int>(repeated_fault_controller.getState())
    << " | step=" << static_cast<int>(repeated_fault_controller.getSequenceStep())
    << " | alarm=" << static_cast<int>(repeated_fault_controller.getAlarmCode())
    << " | pump=" << repeated_fault_device.pump_on
    << " | heater=" << repeated_fault_device.heater_on
    << " | run_led=" << repeated_fault_device.run_led
    << " | error_led=" << repeated_fault_device.error_led
    << " | buzzer=" << repeated_fault_device.buzzer_on
    << '\n';

  return 0;
}