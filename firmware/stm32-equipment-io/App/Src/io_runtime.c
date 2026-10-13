#include "io_runtime.h"
#include "io_protocol.h"
#include "device_io.h"
#include "pressure_adc.h"
#include "uart_io.h"
#include "io_environment.h"
#include <string.h>

static IoLineReader reader;
static char tx[128];
static bool tx_environment, tx_environment_valid;
static uint32_t tx_started;
static size_t tx_length, tx_position;
static uint32_t last_sample;
static bool run_led, error_led;
/* Separate local timeout ownership from the Linux error cache and cold boot. */
static bool communication_loss_red, state_refresh_ready;
static bool reset_level, reset_stable, reset_pending;
static uint32_t reset_changed;

/* Read the existing PC13 input without changing CubeMX/EXTI/NVIC settings.
   One event per debounced press; held-at-boot requires release first. */
static void poll_reset(uint32_t now)
{
  const bool pressed = DeviceIo_ReadResetActive();
  if (pressed != reset_level) { reset_level = pressed; reset_changed = now; }
  if (pressed != reset_stable && (uint32_t)(now - reset_changed) >= 30U)
  {
    reset_stable = pressed;
    if (pressed && io_runtime_control_valid && error_led) reset_pending = true;
  }
  if (!io_runtime_control_valid || !error_led) reset_pending = false;
}

static uint32_t last_control_refresh, refresh_started;
static unsigned refresh_next;
static bool pending[6];
bool io_runtime_control_valid;
bool io_runtime_failsafe_latched;
uint32_t io_runtime_rx_errors;
uint32_t io_runtime_unsupported_valve_commands;

static void apply_state(IoState state)
{
  /* SAFE synchronization and isolated state traffic cannot release local RED.
     Handoff requires a complete refresh followed by a Linux operating state. */
  if (communication_loss_red)
  {
    if (!io_runtime_control_valid || io_runtime_failsafe_latched ||
        !state_refresh_ready || state == IO_STATE_SAFE)
    {
      DeviceIo_SetIndicators(false, true);
      return;
    }
    communication_loss_red = false;
  }
  /* Never display a normal operating state without valid control. */
  if (!io_runtime_control_valid || io_runtime_failsafe_latched)
  {
    DeviceIo_SetIndicators(false, error_led);
    return;
  }

  switch (state)
  {
    case IO_STATE_IDLE:
      DeviceIo_SetStateLed(DEVICE_STATE_IDLE);
      break;
    case IO_STATE_READY:
      DeviceIo_SetStateLed(DEVICE_STATE_READY);
      break;
    case IO_STATE_RUN:
      DeviceIo_SetStateLed(DEVICE_STATE_RUN);
      break;
    case IO_STATE_ERROR:
      DeviceIo_SetIndicators(false, true); // Keep RED high during ownership handoff.
      break;
    case IO_STATE_SAFE:
    default:
      DeviceIo_SetIndicators(false, error_led);
      break;
  }
}

static void apply(const IoCommand *command)
{
  switch (command->kind)
  {
    case IO_PUMP:
      DeviceIo_SetPump(command->on);
      break;

    case IO_HEATER:
      DeviceIo_SetHeater(command->on);
      break;

    case IO_BUZZER:
      DeviceIo_SetBuzzer(command->on);
      break;

    case IO_RUN_LED:
      run_led = command->on;
      break;

    case IO_ERROR_LED:
      error_led = command->on;
      break;

    case IO_VALVE:
      /* No specified valve/stepper mapping: recognize but do not actuate. */
      ++io_runtime_unsupported_valve_commands;
      break;

    case IO_STATE:
      /* Handled separately in receive_command(). */
      break;
  }
}

/* This is physical communication safety, not the Linux equipment state machine. */
static void communication_safe_state(void)
{
  DeviceIo_SetPump(false);
  DeviceIo_SetHeater(false);

  run_led = false;

  /* Called only after previously valid control reaches its existing deadline.
     Do not change the Linux alarm cause or touch the buzzer. */
  communication_loss_red = true;
  state_refresh_ready = false;
  DeviceIo_SetIndicators(false, true);

  io_runtime_control_valid = false;
  io_runtime_failsafe_latched = true;
  refresh_next = 0;

  if (reader.length || reader.discard)
  {
    IoProtocol_Discard(&reader);
  }
}

static void receive_command(const IoCommand *command, uint32_t now)
{
  if (command->kind == IO_STATE)
  {
    apply_state(command->state);
    state_refresh_ready = false;
    return;
  }

  state_refresh_ready = false;
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
        const IoCommand output = {order[i], pending[i], IO_STATE_SAFE};
        apply(&output);
      }
    }
  }
  last_control_refresh = now;
  io_runtime_control_valid = true;
  state_refresh_ready = true;
}

void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart)
{
  IoEnvironment_Reset();
  tx_environment = false;
  reader = (IoLineReader){0};
  tx_length = tx_position = 0;
  run_led = error_led = false;
  communication_loss_red = state_refresh_ready = false;
  reset_level = reset_stable = DeviceIo_ReadResetActive();
  reset_pending = false;
  reset_changed = HAL_GetTick();
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
  if (io_runtime_control_valid &&
      (uint32_t)(HAL_GetTick() - last_control_refresh) >= IO_CONTROL_TIMEOUT_MS)
    communication_safe_state();
  // The capture hook preserves RX bytes while sampling DHT11 pulses.
  IoEnvironment_Poll();
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
  poll_reset(now);
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
      state_refresh_ready = false;
      refresh_next = 0;
      IoProtocol_Discard(&reader);
      break;
    }
    IoCommand command;
    if (IoProtocol_Feed(&reader, byte, &command)) receive_command(&command, now);
    else if (byte == '\n' || reader.discard)
    {
      refresh_next = 0;
      state_refresh_ready = false;
    }
  }
  if (tx_position == tx_length && (uint32_t)(now - last_sample) >= 100U)
  {
    IoSensors sensors = {0};
    sensors.door_closed = DeviceIo_ReadDoorActive();
    sensors.estop = DeviceIo_ReadEmergencyStopActive();
    sensors.pressure_valid = PressureAdc_Read(&sensors.pressure);
    IoEnvironment_Take(&sensors, now);
    tx_environment = sensors.environment_present;
    tx_environment_valid = sensors.environment_valid;
    tx_started = now;
    tx_length = IoProtocol_Serialize(&sensors, tx, sizeof(tx));
    if (reset_pending && tx_length && sensors.door_closed && !sensors.estop)
    {
      memcpy(tx + tx_length, "RESET\n", 6);
      tx_length += 6;
    }
    reset_pending = false;
    tx_position = 0;
    last_sample = now;
  }
  if (tx_environment && tx_position < tx_length &&
      ((uint32_t)(now - tx_started) >= IO_ENVIRONMENT_TIMEOUT_MS ||
       (tx_environment_valid && !IoEnvironment_Valid(now))))
  {
    /* Do not release a stale queued value after prolonged TX backpressure.
       '!' makes any partially sent numeric line invalid before resynchronizing. */
    const char invalid[] = "!\nTEMP:INVALID\nHUMIDITY:INVALID\n";
    memcpy(tx, invalid, sizeof(invalid) - 1U);
    tx_length = sizeof(invalid) - 1U;
    tx_position = 0;
    tx_environment = false;
  }
  for (unsigned i = 0; i < 32 && tx_position < tx_length; ++i)
  {
    if (!UartIo_TryWrite((uint8_t)tx[tx_position])) break;
    ++tx_position;
  }
}
