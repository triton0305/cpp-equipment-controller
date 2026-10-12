#include "io_runtime.h"
#include "io_protocol.h"
#include "device_io.h"
#include "pressure_adc.h"
#include "uart_io.h"

static IoLineReader reader;
static char tx[64];
static size_t tx_length, tx_position;
static uint32_t last_sample;
static bool run_led, error_led;
static uint32_t last_control_refresh, refresh_started;
static unsigned refresh_next;
static bool pending[6];
bool io_runtime_control_valid;
bool io_runtime_failsafe_latched;
uint32_t io_runtime_rx_errors;
uint32_t io_runtime_unsupported_valve_commands;

static void apply(const IoCommand *command)
{
  switch (command->kind)
  {
    case IO_PUMP: DeviceIo_SetPump(command->on); break;
    case IO_HEATER: DeviceIo_SetHeater(command->on); break;
    case IO_BUZZER: DeviceIo_SetBuzzer(command->on); break;
    case IO_RUN_LED:
      run_led = command->on;
      DeviceIo_SetIndicators(run_led, error_led);
      break;
    case IO_ERROR_LED:
      error_led = command->on;
      DeviceIo_SetIndicators(run_led, error_led);
      break;
    case IO_VALVE:
      /* No specified valve/stepper mapping: recognize but do not actuate. */
      ++io_runtime_unsupported_valve_commands;
      break;
  }
}

/* This is physical communication safety, not the Linux equipment state machine. */
static void communication_safe_state(void)
{
  DeviceIo_SetPump(false);
  DeviceIo_SetHeater(false);
  run_led = false; /* Keep the actual output and the indicator cache consistent. */
  DeviceIo_SetIndicators(run_led, error_led);
  /* Buzzer/error policy belongs to Linux. Valve has no actuation path. */
  io_runtime_control_valid = false;
  io_runtime_failsafe_latched = true;
  refresh_next = 0;
  if (reader.length || reader.discard) IoProtocol_Discard(&reader);
}

static void receive_command(const IoCommand *command, uint32_t now)
{
  static const IoCommandKind order[] = {
    IO_PUMP, IO_HEATER, IO_VALVE, IO_BUZZER, IO_RUN_LED, IO_ERROR_LED
  };
  const bool critical = command->kind == IO_PUMP ||
                        command->kind == IO_HEATER || command->kind == IO_RUN_LED;
  /* OFF is always immediate. ON is never applied from an incomplete refresh.
     Preserve the existing Linux-owned buzzer/error and unsupported-valve handling. */
  if (!critical || !command->on) apply(command);

  if (command->kind == IO_PUMP)
  {
    refresh_next = 0;
    refresh_started = now;
  }
  if (command->kind != order[refresh_next])
  {
    refresh_next = 0;
    return;
  }
  pending[refresh_next++] = command->on;
  if (refresh_next != sizeof(order) / sizeof(order[0])) return;
  refresh_next = 0;

  if (io_runtime_failsafe_latched)
  {
    /* Require explicit PUMP/HEATER/RUN OFF and VALVE CLOSED. Never replay ON. */
    if (pending[0] || pending[1] || pending[2] || pending[4]) return;
    io_runtime_failsafe_latched = false;
  }
  else
  {
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
    {
      if (order[i] == IO_PUMP || order[i] == IO_HEATER || order[i] == IO_RUN_LED)
      {
        const IoCommand output = {order[i], pending[i]};
        apply(&output);
      }
    }
  }
  last_control_refresh = now;
  io_runtime_control_valid = true;
}

void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart)
{
  reader = (IoLineReader){0};
  tx_length = tx_position = 0;
  run_led = error_led = false;
  io_runtime_rx_errors = io_runtime_unsupported_valve_commands = 0;
  refresh_next = 0;
  io_runtime_control_valid = false;
  io_runtime_failsafe_latched = true;
  DeviceIo_SetSafeState();
  PressureAdc_Init(adc);
  UartIo_Init(uart);
  last_sample = last_control_refresh = HAL_GetTick();
}

void IoRuntime_Poll(void)
{
  const uint32_t now = HAL_GetTick();
  /* Check before RX: bytes arriving at the deadline cannot rescue stale control. */
  if (io_runtime_control_valid &&
      (uint32_t)(now - last_control_refresh) >= IO_CONTROL_TIMEOUT_MS)
    communication_safe_state();
  if (refresh_next && (uint32_t)(now - refresh_started) >= IO_CONTROL_REFRESH_WINDOW_MS)
  {
    refresh_next = 0;
    if (reader.length || reader.discard) IoProtocol_Discard(&reader);
  }
  PressureAdc_Poll(now);
  /* Bounded work, independent RX and TX; no IRQ/peripheral reconfiguration. */
  for (unsigned i = 0; i < 32; ++i)
  {
    uint8_t byte;
    int result = UartIo_TryRead(&byte);
    if (result == 0) break;
    if (result < 0)
    {
      ++io_runtime_rx_errors;
      refresh_next = 0;
      IoProtocol_Discard(&reader);
      break;
    }
    IoCommand command;
    if (IoProtocol_Feed(&reader, byte, &command)) receive_command(&command, now);
    else if (byte == '\n' || reader.discard) refresh_next = 0;
  }
  if (tx_position == tx_length && (uint32_t)(now - last_sample) >= 100U)
  {
    IoSensors sensors = {0};
    sensors.door_closed = DeviceIo_ReadDoorActive();
    sensors.estop = DeviceIo_ReadEmergencyStopActive();
    sensors.pressure_valid = PressureAdc_Read(&sensors.pressure);
    tx_length = IoProtocol_Serialize(&sensors, tx, sizeof(tx));
    tx_position = 0;
    last_sample = now;
  }
  for (unsigned i = 0; i < 32 && tx_position < tx_length; ++i)
  {
    if (!UartIo_TryWrite((uint8_t)tx[tx_position])) break;
    ++tx_position;
  }
}
