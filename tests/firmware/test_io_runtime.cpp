extern "C" {
#include "io_runtime.h"
#include "device_io.h"
#include "uart_io.h"
#include "main.h"
}
#include "ascii_protocol.hpp"
#include "equipment_controller.hpp"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
GPIO_TypeDef fake_gpio[3]{};
bool fake_adc_eoc;
static uint32_t tick;
static uint32_t raw_adc = 2048;
static bool adc_fail;
static std::deque<int> rx;
static std::string tx;
static unsigned tx_budget = 10000;
static unsigned reads, writes;
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *) { fake_adc_eoc = !adc_fail; return adc_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *) { return HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *) { return raw_adc; }
uint32_t HAL_GetTick(void) { return tick; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) { return p->input & pin ? GPIO_PIN_SET : GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState state)
{ if (state == GPIO_PIN_SET) p->output |= pin; else p->output &= ~pin; }
void UartIo_Init(UART_HandleTypeDef *) {}
int UartIo_TryRead(uint8_t *b)
{
  ++reads;
  if (rx.empty()) return 0;
  int v = rx.front(); rx.pop_front();
  if (v < 0) return -1;
  *b = static_cast<uint8_t>(v); return 1;
}
bool UartIo_TryWrite(uint8_t b)
{
  ++writes;
  if (!tx_budget) return false;
  --tx_budget; tx += static_cast<char>(b); return true;
}
static void poll()
{
  reads = writes = 0; IoRuntime_Poll();
  CHECK(reads <= 32 && writes <= 32);
}
static void send(const std::string &s)
{
  for (unsigned char c : s) rx.push_back(c);
  for (unsigned i = 0; i < 100 && !rx.empty(); ++i) poll();
  CHECK(rx.empty());
}
static bool output(GPIO_TypeDef *p, uint16_t pin) { return (p->output & pin) != 0; }
static void check_devices(const DeviceState &d)
{
  CHECK(output(PUMP_LED_GPIO_Port, PUMP_LED_Pin) == d.pump_on);
  CHECK(output(HEATER_LED_GPIO_Port, HEATER_LED_Pin) == d.heater_on);
  CHECK(output(ALARM_BUZZER_GPIO_Port, ALARM_BUZZER_Pin) == d.buzzer_on);
  CHECK(output(STATE_G_GPIO_Port, STATE_G_Pin) == d.run_led);
  CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin) == d.error_led);
  CHECK(!output(STATE_B_GPIO_Port, STATE_B_Pin));
  CHECK((GPIOC->output & 15) == 0); // VALVE must not actuate stepper.
}
int main()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  for (auto &p : fake_gpio) p.output = 0xffff;
  IoRuntime_Init(&adc, &uart);
  check_devices(DeviceState{});
  GPIOB->input = DOOR_SW_Pin | ESTOP_SW_Pin;
  CHECK(!DeviceIo_ReadDoorActive() && !DeviceIo_ReadEmergencyStopActive());
  GPIOB->input = 0;
  CHECK(DeviceIo_ReadDoorActive() && DeviceIo_ReadEmergencyStopActive());
  for (auto state : {DEVICE_STATE_IDLE, DEVICE_STATE_READY, DEVICE_STATE_RUN, DEVICE_STATE_ERROR})
  {
    DeviceIo_SetStateLed(state);
    CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin) == (state == DEVICE_STATE_READY || state == DEVICE_STATE_ERROR));
    CHECK(output(STATE_G_GPIO_Port, STATE_G_Pin) == (state == DEVICE_STATE_READY || state == DEVICE_STATE_RUN));
    CHECK(output(STATE_B_GPIO_Port, STATE_B_Pin) == (state == DEVICE_STATE_IDLE));
  }
  for (unsigned mask = 0; mask < 64; ++mask)
  {
    DeviceState d;
    d.pump_on = mask & 1; d.heater_on = mask & 2; d.valve_open = mask & 4;
    d.buzzer_on = mask & 8; d.run_led = mask & 16; d.error_led = mask & 32;
    send(AsciiProtocol::serializeDeviceState(d)); check_devices(d);
  }
  CHECK(io_runtime_unsupported_valve_commands == 64);
  send("PUMP:OFF\nPUMP:"); rx.push_back(-1); poll();
  send("ON\n"); CHECK(!output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  CHECK(io_runtime_rx_errors == 1);
  send(std::string(100, 'x') + "PUMP:ON\n");
  CHECK(!output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  // TX backpressure must neither block reception nor reorder/truncate the packet.
  GPIOB->input = ESTOP_SW_Pin; // door closed, E-stop released
  tx_budget = 0; tick = 100; poll();
  CHECK(tx.empty()); send("PUMP:ON\n"); CHECK(output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  tx_budget = 1; poll(); CHECK(tx.size() == 1);
  tx_budget = 10000; poll(); poll();
  CHECK(tx == "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\n");
  // Host-only integration: real STM32 serialization -> real Linux decisions -> real STM32 GPIO layer (fake HAL).
  EquipmentController controller; SensorState sensors;
  auto consume = [&]() {
    std::istringstream lines(tx); std::string line;
    while (std::getline(lines, line))
    {
      CHECK(AsciiProtocol::parseSensorLine(line, sensors));
      controller.setSensorState(sensors); controller.update();
      send(AsciiProtocol::serializeDeviceState(controller.getDeviceState()));
    }
    tx.clear();
  };
  consume(); controller.handleCommand(Command{CommandType::Start});
  for (unsigned i = 0; i < 6; ++i) controller.update();
  CHECK(controller.getDeviceState().pump_on);
  send(AsciiProtocol::serializeDeviceState(controller.getDeviceState()));
  check_devices(controller.getDeviceState());
  GPIOB->input = 0; // physical E-stop simulated active-low
  tick = 200; poll(); poll(); consume();
  CHECK(controller.getState() == EquipmentState::Error);
  CHECK(controller.getAlarmCode() == AlarmCode::EmergencyStop);
  CHECK(!controller.getDeviceState().pump_on && !controller.getDeviceState().heater_on);
  CHECK(controller.getDeviceState().buzzer_on);
  check_devices(controller.getDeviceState());
  // Door-open and low-pressure faults through the same actual host/controller path.
  for (bool door_fault : {true, false})
  {
    controller = EquipmentController{}; sensors = SensorState{};
    sensors.pressure = 2048;
    controller.setSensorState(sensors);
    controller.handleCommand(Command{CommandType::Start});
    for (unsigned i = 0; i < 4; ++i) controller.update();
    GPIOB->input = ESTOP_SW_Pin | (door_fault ? DOOR_SW_Pin : 0);
    raw_adc = door_fault ? 2048 : 0;
    poll(); poll(); tick += 100; poll(); poll(); consume();
    CHECK(controller.getState() == EquipmentState::Error);
    CHECK(controller.getAlarmCode() == (door_fault ? AlarmCode::DoorOpen : AlarmCode::PressureFault));
    check_devices(controller.getDeviceState());
  }
  // ADC failure omits pressure, no fabricated zero or stale pressure report.
  adc_fail = true; poll(); poll(); tick += 100; poll(); poll();
  CHECK(tx.find("PRESSURE:") == std::string::npos); tx.clear();
  // Scheduler remains valid across the HAL tick wrap.
  tick = UINT32_MAX - 49; IoRuntime_Init(&adc, &uart);
  tick = 49; poll(); CHECK(tx.empty());
  tick = 50; poll(); poll(); CHECK(!tx.empty());
  DeviceIo_SetSafeState(); check_devices(DeviceState{});
}
