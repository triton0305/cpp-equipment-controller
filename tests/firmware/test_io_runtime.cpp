extern "C" {
#include "io_runtime.h"
#include "io_environment.h"
#include "dht11.h"
#include "device_io.h"
#include "uart_io.h"
#include "main.h"
}
#include "ascii_protocol.hpp"
#include "equipment_controller.hpp"
#include "../core/test_sensor_state.hpp"
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
static bool require_red_high;
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *) { fake_adc_eoc = !adc_fail; return adc_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *) { return HAL_OK; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *) { return raw_adc; }
uint32_t HAL_GetTick(void) { return tick; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin) { return p->input & pin ? GPIO_PIN_SET : GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState state)
{
  if (require_red_high && p == STATE_R_GPIO_Port && (pin & STATE_R_Pin))
    CHECK(state == GPIO_PIN_SET); // Detect even a transient OFF write during handoff.
  if (state == GPIO_PIN_SET) p->output |= pin; else p->output &= ~pin;
}
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
static Dht11Result dht_init_result = DHT11_OK;
static Dht11Result dht_result = DHT11_NOT_READY;
static Dht11Sample dht_sample{452, 259};
static uint32_t dht_delay;
static void (*dht_service)(void);
Dht11Result Dht11_Init(TIM_HandleTypeDef *) { return dht_init_result; }
Dht11Result Dht11_Request(void) { return DHT11_NOT_READY; }
Dht11Result Dht11_Poll(Dht11Sample *sample)
{
  tick += dht_delay; dht_delay = 0;
  if (dht_service) dht_service();
  const auto result = dht_result; dht_result = DHT11_NOT_READY;
  if (result == DHT11_OK) *sample = dht_sample;
  return result;
}
void Dht11_Cancel(void) {}
void Dht11_SetServiceHook(void (*service)(void)) { dht_service = service; }
void UartIo_BufferRx(void) {}

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
  CHECK((GPIOC->output & 15) == 0);
}

static void test_communication_failsafe()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  const uint32_t timeout = IO_CONTROL_TIMEOUT_MS;
  const uint32_t window = IO_CONTROL_REFRESH_WINDOW_MS;
  DeviceState safe, active;
  active.pump_on = active.heater_on = active.run_led = true;
  active.buzzer_on = active.error_led = true;
  const auto safe_packet = AsciiProtocol::serializeDeviceState(safe);
  const auto active_packet = AsciiProtocol::serializeDeviceState(active);
  auto init = [&](uint32_t start = 0) {
    rx.clear(); tx.clear(); tick = start; tx_budget = 10000;
    IoRuntime_Init(&adc, &uart);
    CHECK(!io_runtime_control_valid && io_runtime_failsafe_latched);
    check_devices(safe);
  };
  auto latched = [&]() {
    CHECK(!io_runtime_control_valid && io_runtime_failsafe_latched);
    CHECK(!output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
    CHECK(!output(HEATER_LED_GPIO_Port, HEATER_LED_Pin));
    CHECK(!output(STATE_G_GPIO_Port, STATE_G_Pin));
  };
  auto running = [&]() {
    send(safe_packet);
    CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
    send(active_packet); check_devices(active);
  };
  // Startup cannot energize outputs, including through a complete unsafe packet.
  init(); send("PUMP:ON\nHEATER:ON\nRUN_LED:ON\n"); latched();
  send(active_packet); latched();
  send("PUMP:OFF\nHEATER:OFF\nVALVE:OPEN\nBUZZER:OFF\nRUN_LED:OFF\nERROR_LED:OFF\n");
  latched(); // CLOSED is required for safe resynchronization, even without actuation.
  send(safe_packet); check_devices(safe);
  CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
  send("PUMP:ON\nHEATER:ON\n"); check_devices(safe);
  send("VALVE:CLOSED\nBUZZER:ON\nRUN_LED:ON\nERROR_LED:ON\n");
  check_devices(active);

  // Each unsafe field independently blocks synchronization; alarm outputs need not be OFF.
  for (unsigned field = 0; field < 4; ++field)
  {
    init(); DeviceState unsafe;
    unsafe.pump_on = field == 0; unsafe.heater_on = field == 1;
    unsafe.run_led = field == 2; unsafe.valve_open = field == 3;
    send(AsciiProtocol::serializeDeviceState(unsafe)); latched();
  }
  init(); DeviceState alarm; alarm.buzzer_on = alarm.error_led = true;
  send(AsciiProtocol::serializeDeviceState(alarm)); check_devices(alarm);
  CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
  // Continuous complete refreshes keep active control alive beyond multiple deadlines.
  init(); running();
  for (unsigned i = 0; i < 30; ++i)
  {
    tick += 100; send(active_packet); check_devices(active);
    CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
  }

  // Independently fix T-1, T and T+1 semantics with no RX at all.
  for (uint32_t elapsed : {timeout - 1, timeout, timeout + 1})
  {
    init(); running(); tick = elapsed; poll();
    if (elapsed < timeout) { check_devices(active); CHECK(io_runtime_control_valid); }
    else
    {
      latched();
      DeviceState fault = active; fault.pump_on = fault.heater_on = fault.run_led = false;
      check_devices(fault); // Buzzer/error retained; valve coils still OFF.
      send("ERROR_LED:OFF\n");
      CHECK(!output(STATE_G_GPIO_Port, STATE_G_Pin)); // No stale RUN cache resurrection.
      send("PUMP:ON\nHEATER:ON\nRUN_LED:ON\n"); latched();
      send(active_packet); latched(); // Complete but unsafe does not unlock.
      send("PUMP:OFF\nHEATER:OFF\nVALVE:CLOSED\nBUZZER:OFF\nRUN_LED:OFF\n"); latched();
      send("ERROR_LED:OFF\n"); check_devices(safe);
      CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
      send("HEATER:ON\nRUN_LED:ON\n"); check_devices(safe);
      send(active_packet); check_devices(active);
    }
  }

  // Partial, malformed, unknown, oversized and binary RX never keep control alive.
  for (const auto &noise : {std::string("PUMP:ON\n"), std::string("RUN_LED:ON\n"),
       std::string("RUN_LED:ON\nERROR_LED:ON\n"), std::string("PUMP:INVALID\n"),
       std::string("UNKNOWN:ON\n"), std::string("PUMP:"), std::string(80, 'x') + "\n",
       std::string("\0\xff\n", 3)})
  {
    init(); running();
    for (unsigned i = 1; i <= 10; ++i) { tick = i * (timeout / 10); send(noise); }
    latched();
  }

  // Every command is required, in serializer order; bad lines/errors invalidate progress.
  const std::string lines[] = {"PUMP:ON\n", "HEATER:ON\n", "VALVE:CLOSED\n",
    "BUZZER:ON\n", "RUN_LED:ON\n", "ERROR_LED:ON\n"};
  for (unsigned missing = 0; missing < 6; ++missing)
  {
    init(); running(); tick = timeout - 1;
    for (unsigned i = 0; i < 6; ++i) if (i != missing) send(lines[i]);
    tick = timeout; poll(); latched();
  }
  for (const auto &bad : {std::string("BOGUS\n"), std::string("\n"),
       std::string("HEATER:OFF\n"), std::string("\0", 1), std::string(40, 'x') + "\n"})
  {
    init(); running(); tick = timeout - 1;
    send(lines[0] + lines[1] + bad);
    for (unsigned i = 2; i < 6; ++i) send(lines[i]);
    tick = timeout; poll(); latched();
  }
  init(); running(); tick = timeout - 1;
  send(lines[0] + lines[1]); rx.push_back(-1); poll(); send("\n");
  for (unsigned i = 2; i < 6; ++i) send(lines[i]);
  tick = timeout; poll(); latched(); CHECK(io_runtime_rx_errors == 1);

  // The last completed refresh, never its first/partial lines, sets the deadline.
  init(); running(); tick = timeout - 20; send(active_packet);
  tick += timeout - 1; poll(); check_devices(active);
  ++tick; poll(); latched();
  // Deadline is checked before a queued unsafe complete packet is consumed.
  init(); running(); tick = timeout; send(active_packet); latched();
  // A pre-timeout partial line cannot combine with post-timeout data to unlock.
  init(); running(); tick = timeout - 1; send("PUMP:OFF\nHEATER:");
  tick = timeout; send("OFF\nVALVE:CLOSED\nBUZZER:OFF\nRUN_LED:OFF\nERROR_LED:OFF\n"); latched();
  send(safe_packet); CHECK(!io_runtime_failsafe_latched); check_devices(safe);

  const auto safe_tail = safe_packet.substr(safe_packet.find('\n') + 1);
  for (uint32_t elapsed : {window - 1, window, window + 1})
  {
    init(); send("PUMP:OFF\n"); tick = elapsed; send(safe_tail);
    if (elapsed < window) CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
    else latched();
  }
  init(); send("PUMP:OFF\nHEATER:"); tick = window;
  send("OFF\nVALVE:CLOSED\nBUZZER:OFF\nRUN_LED:OFF\nERROR_LED:OFF\n"); latched();
  send(safe_packet); CHECK(!io_runtime_failsafe_latched);
  // Restart on a fresh PUMP, CRLF and bytewise fragmented input are supported.
  init(); send("PUMP:ON\nHEATER:ON\n");
  std::string crlf;
  for (char c : safe_packet) { if (c == '\n') crlf += '\r'; crlf += c; }
  for (char c : crlf) send(std::string(1, c));
  CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched); check_devices(safe);
  // OFF commands still cut outputs immediately, even without a complete packet.
  send(active_packet); send("PUMP:OFF\nHEATER:OFF\nRUN_LED:OFF\n");
  CHECK(!output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  CHECK(!output(HEATER_LED_GPIO_Port, HEATER_LED_Pin));
  CHECK(!output(STATE_G_GPIO_Port, STATE_G_Pin));
  // Both deadlines use unsigned subtraction across uint32 rollover.
  init(UINT32_MAX - 100); running();
  tick += timeout - 1; poll(); check_devices(active);
  ++tick; poll(); latched();
  init(UINT32_MAX - 100); send("PUMP:OFF\n"); tick += window - 1; send(safe_tail);
  CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
  init(UINT32_MAX - 100); send("PUMP:OFF\n"); tick += window; send(safe_tail); latched();
  init(); tick = timeout + 1; poll(); latched(); // Startup with no RX remains invalid.
}

static void test_communication_red()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  DeviceState safe, run;
  run.pump_on = run.heater_on = run.run_led = true;
  const auto safe_packet = AsciiProtocol::serializeDeviceState(safe);
  const auto run_packet = AsciiProtocol::serializeDeviceState(run);
  auto init = [&]() {
    require_red_high = false;
    rx.clear(); tx.clear(); tick = 0; tx_budget = 10000;
    IoRuntime_Init(&adc, &uart);
    check_devices(safe);
    CHECK(!output(STATE_R_GPIO_Port, STATE_R_Pin));
    CHECK(!output(STATE_G_GPIO_Port, STATE_G_Pin));
  };
  auto red_safe = [&]() {
    CHECK(!io_runtime_control_valid && io_runtime_failsafe_latched);
    check_devices(safe); // Includes buzzer OFF: loss itself never enables it.
    CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin));
    CHECK(!output(STATE_G_GPIO_Port, STATE_G_Pin));
    CHECK(!output(STATE_B_GPIO_Port, STATE_B_Pin));
  };
  init(); tick = IO_CONTROL_TIMEOUT_MS + 1; poll();
  CHECK(!output(STATE_R_GPIO_Port, STATE_R_Pin)); // Silent cold boot is not loss.
  for (const auto& noise : {std::string("PUMP:"), std::string("HEATER:bad\n"),
      std::string("UNKNOWN:OFF\n"), std::string("STATE:SAFE\n"),
      std::string("STATE:IDLE\n"), std::string("ERROR_LED:OFF\n"),
      std::string("PUMP:OFF\nHEATER:OFF\nVALVE:CLOSED\nBUZZER:OFF\nRUN_LED:OFF\n")})
  {
    init(); send(safe_packet); send(run_packet + "STATE:RUN\n");
    tick = IO_CONTROL_TIMEOUT_MS - 1; poll();
    check_devices(run); CHECK(io_runtime_control_valid);
    CHECK(!output(STATE_R_GPIO_Port, STATE_R_Pin));
    tick = IO_CONTROL_TIMEOUT_MS; poll(); red_safe();
    require_red_high = true;
    ++tick; poll(); red_safe();
    send(noise); red_safe();
  }
  // Linux reconnect sends SAFE first, then the latched Alarm=6 ERROR snapshot.
  init(); send(safe_packet); send(run_packet + "STATE:RUN\n");
  tick = IO_CONTROL_TIMEOUT_MS; poll(); red_safe(); require_red_high = true;
  send("\n" + safe_packet + "STATE:SAFE\n");
  CHECK(io_runtime_control_valid && !io_runtime_failsafe_latched);
  check_devices(safe); CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin));
  send("STATE:IDLE\nERROR_LED:OFF\nSTATE:SAFE\n");
  CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin));
  DeviceState alarm; alarm.error_led = alarm.buzzer_on = true;
  send(AsciiProtocol::serializeDeviceState(alarm) + "STATE:SAFE\n");
  CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin));
  send(AsciiProtocol::serializeDeviceState(alarm) + "STATE:ERROR\n");
  check_devices(alarm); CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin));
  tick += IO_CONTROL_TIMEOUT_MS; poll(); // Already in Linux Error, then loss again.
  CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin)); check_devices(alarm);
  send(AsciiProtocol::serializeDeviceState(alarm) + "STATE:ERROR\n");
  check_devices(alarm);
  require_red_high = false;
  send(safe_packet + "STATE:IDLE\n"); // Linux accepted RESET owns indications again.
  check_devices(safe);
  CHECK(!output(STATE_R_GPIO_Port, STATE_R_Pin));
  CHECK(output(STATE_B_GPIO_Port, STATE_B_Pin));
}

static void test_state_protocol()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  rx.clear(); tx.clear(); tick = 0; tx_budget = 10000;
  IoRuntime_Init(&adc, &uart);
  const auto rgb = [](bool red, bool green, bool blue) {
    CHECK(output(STATE_R_GPIO_Port, STATE_R_Pin) == red);
    CHECK(output(STATE_G_GPIO_Port, STATE_G_Pin) == green);
    CHECK(output(STATE_B_GPIO_Port, STATE_B_Pin) == blue);
  };
  send("STATE:READY\nSTATE:RUN\n"); rgb(false, false, false);
  CHECK(io_runtime_failsafe_latched);
  send(AsciiProtocol::serializeDeviceState(DeviceState{}));
  send("STATE:IDLE\n"); rgb(false, false, true);
  send("STATE:READY\n"); rgb(true, true, false);
  send("STATE:RUN\n"); rgb(false, true, false);
  send("STATE:ERROR\n"); rgb(true, false, false);
  send("STATE:SAFE\n"); rgb(false, false, false);
  send("STATE:IDLE\nRUN_LED:ON\nERROR_LED:ON\n"); rgb(false, false, true);
  // Error cache is retained across command-loss and subsequent SAFE refresh.
  tick = IO_CONTROL_TIMEOUT_MS; poll(); rgb(true, false, false);
  send("STATE:READY\n"); rgb(true, false, false);
  send("STATE:RUN\n"); rgb(true, false, false);
  DeviceState alarm; alarm.error_led = true;
  send(AsciiProtocol::serializeDeviceState(alarm) + "STATE:SAFE\n"); rgb(true, false, false);
  send(AsciiProtocol::serializeDeviceState(DeviceState{}) + "STATE:IDLE\n"); rgb(false, false, true);
}

static void test_reset_button()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  rx.clear(); tx.clear(); tick = 0; tx_budget = 10000; adc_fail = false;
  GPIOC->input = B1_Pin; GPIOB->input = ESTOP_SW_Pin;
  IoRuntime_Init(&adc, &uart);
  DeviceState alarm; alarm.error_led = true;
  send(AsciiProtocol::serializeDeviceState(alarm));
  GPIOC->input = 0; tick = 10; poll();
  tick = 39; poll(); CHECK(tx.find("RESET") == std::string::npos);
  GPIOC->input = B1_Pin; poll(); // bounce resets debounce
  GPIOC->input = 0; tick = 50; poll();
  tick = 80; poll(); tick = 100; poll(); poll();
  CHECK(tx == "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nRESET\n");
  tx.clear(); tick = 200; poll(); poll(); CHECK(tx.find("RESET") == std::string::npos);
  // Held button does not repeat, including after MCU initialization.
  tx.clear(); IoRuntime_Init(&adc, &uart); send(AsciiProtocol::serializeDeviceState(alarm));
  tick = 300; poll(); poll(); CHECK(tx.find("RESET") == std::string::npos);
  tx.clear(); GPIOC->input = B1_Pin; tick = 310; poll(); tick = 340; poll();
  GPIOB->input = 0; // E-stop blocks reset transmission
  GPIOC->input = 0; tick = 350; poll(); tick = 380; poll();
  tick = 400; poll(); poll(); CHECK(tx.find("RESET") == std::string::npos);
}

static void test_environment()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{}; TIM_HandleTypeDef timer{};
  auto init = [&]() {
    rx.clear(); tx.clear(); tick = 0; tx_budget = 10000; adc_fail = false;
    GPIOB->input = ESTOP_SW_Pin; GPIOC->input = B1_Pin;
    IoRuntime_Init(&adc, &uart); IoEnvironment_Init(&timer);
    poll(); poll();
  };
  init(); CHECK(dht_service != nullptr);
  tick = 100; poll(); poll(); poll();
  CHECK(tx == "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:INVALID\nHUMIDITY:INVALID\n");
  tx.clear(); tick = 2120; dht_result = DHT11_OK; poll(); poll(); poll();
  CHECK(tx == "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25.9\nHUMIDITY:45.2\n");
  SensorState sensor;
  std::istringstream lines(tx); std::string line;
  while (std::getline(lines, line)) CHECK(AsciiProtocol::parseSensorLine(line, sensor));
  CHECK(sensor.temperature_valid && sensor.humidity_valid);
  CHECK(sensor.temperature == 25.9 && sensor.humidity == 45.2);
  // No cached measurements repeated by the 100ms Door/E-Stop/Pressure refresh.
  tx.clear(); tick += 100; poll(); poll();
  CHECK(tx.find("TEMP:") == std::string::npos && tx.find("PRESSURE:2048") != std::string::npos);
  for (auto result : {DHT11_RESPONSE_TIMEOUT, DHT11_DATA_TIMEOUT, DHT11_CHECKSUM_ERROR,
                      DHT11_INVALID_DATA, DHT11_HAL_ERROR})
  {
    tx.clear(); tick += 2100; dht_result = result; poll(); poll(); poll();
    CHECK(tx.find("TEMP:INVALID\nHUMIDITY:INVALID\n") != std::string::npos);
    CHECK(tx.find("25.9") == std::string::npos);
    tx.clear(); tick += 2100; dht_result = DHT11_OK; poll(); poll(); poll();
    CHECK(tx.find("TEMP:25.9\nHUMIDITY:45.2\n") != std::string::npos);
  }
  // Deadline wrap and stale results while the sensor driver stops delivering.
  init(); tick = UINT32_MAX - 2000; dht_result = DHT11_OK; poll(); poll(); poll();
  tx.clear(); tick += 4999; poll(); poll(); poll();
  CHECK(tx.find("TEMP:") == std::string::npos);
  tx.clear(); tick += 100; poll(); poll(); poll();
  CHECK(tx.find("TEMP:INVALID\nHUMIDITY:INVALID\n") != std::string::npos);
  // TX stalls cannot replay old numeric samples on recovery.
  init(); tx_budget = 0; tick = 2120; dht_result = DHT11_OK; poll();
  tick += IO_ENVIRONMENT_TIMEOUT_MS; poll();
  tx_budget = 10000; poll(); poll();
  CHECK(tx.find("25.9") == std::string::npos);
  CHECK(tx.find("TEMP:INVALID\nHUMIDITY:INVALID\n") != std::string::npos);
  // Failure supersedes a partially transmitted previous result, without creating
  // a valid truncated number when the blocked TX finally resumes.
  init(); tick = 2120; dht_result = DHT11_OK; tx_budget = 44;
  poll(); poll(); // Prefix ends partway through TEMP.
  tick += 2100; dht_result = DHT11_CHECKSUM_ERROR; poll();
  tx_budget = 10000; poll(); poll(); poll();
  CHECK(tx.find("TEMP:25.9\n") == std::string::npos);
  CHECK(tx.find("TEMP:INVALID\nHUMIDITY:INVALID\n") != std::string::npos);
  // A capture crossing the control deadline cannot let queued ON refresh rescue it.
  init(); send(AsciiProtocol::serializeDeviceState(DeviceState{}));
  DeviceState active; active.pump_on = active.heater_on = active.run_led = true;
  send(AsciiProtocol::serializeDeviceState(active));
  tick = IO_CONTROL_TIMEOUT_MS - 1; dht_delay = 6;
  for (auto c : AsciiProtocol::serializeDeviceState(active)) rx.push_back(c);
  poll(); while (!rx.empty()) poll();
  CHECK(io_runtime_failsafe_latched && !io_runtime_control_valid);
  CHECK(!output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  CHECK(!output(HEATER_LED_GPIO_Port, HEATER_LED_Pin));
  dht_init_result = DHT11_HAL_ERROR; init(); CHECK(dht_service == nullptr);
  tick = 100; poll(); poll(); poll();
  CHECK(tx.find("TEMP:INVALID\nHUMIDITY:INVALID\n") != std::string::npos);
  std::cout << "STM32 DHT11 production adapter Host PASS\n";
}

int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--environment")
  { test_environment(); return 0; }
  if (argc == 2 && std::string(argv[1]) == "--failsafe")
  { test_communication_failsafe(); test_communication_red(); return 0; }
  test_state_protocol();
  test_reset_button();
  rx.clear(); tx.clear(); tick = 0; tx_budget = 10000;
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
  CHECK(tx.empty());
  DeviceState active; active.pump_on = true;
  send(AsciiProtocol::serializeDeviceState(active)); CHECK(output(PUMP_LED_GPIO_Port, PUMP_LED_Pin));
  tx_budget = 1; poll(); CHECK(tx.size() == 1);
  tx_budget = 10000; poll(); poll();
  CHECK(tx == "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\n");
  // Host-only integration: real STM32 serialization -> real Linux decisions -> real STM32 GPIO layer (fake HAL).
  EquipmentController controller; SensorState sensors = normalSensors();
  auto consume = [&]() {
    std::istringstream lines(tx); std::string line;
    while (std::getline(lines, line))
    {
      CHECK(AsciiProtocol::parseSensorLine(line, sensors));
      controller.setSensorState(sensors); controller.checkSafety();
      if (line.rfind("PRESSURE:", 0) == 0) controller.observeMeasurement(SensorKind::Pressure);
      send(AsciiProtocol::serializeDeviceState(controller.getDeviceState()));
    }
    tx.clear();
  };
  consume(); controller.handleCommand(Command{CommandType::Start});
  for (unsigned i = 0; i < 6; ++i) controller.update(std::chrono::seconds(12));
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
    controller = EquipmentController{}; sensors = normalSensors();
    sensors.pressure = 2048;
    controller.setSensorState(sensors);
    controller.handleCommand(Command{CommandType::Start});
    for (unsigned i = 0; i < 4; ++i) controller.update(std::chrono::seconds(12));
    GPIOB->input = ESTOP_SW_Pin | (door_fault ? DOOR_SW_Pin : 0);
    raw_adc = door_fault ? 2048 : 0;
    poll(); poll(); tick += 100; poll(); poll(); consume();
    if (!door_fault)
    {
      CHECK(controller.getState() != EquipmentState::Error);
      tick += 100; poll(); poll(); consume();
    }
    CHECK(controller.getState() == EquipmentState::Error);
    CHECK(controller.getAlarmCode() == (door_fault ? AlarmCode::DoorOpen : AlarmCode::PressureCritical));
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
