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

void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart)
{
  reader = (IoLineReader){0};
  tx_length = tx_position = 0;
  run_led = error_led = false;
  io_runtime_rx_errors = io_runtime_unsupported_valve_commands = 0;
  DeviceIo_SetSafeState();
  PressureAdc_Init(adc);
  UartIo_Init(uart);
  last_sample = HAL_GetTick();
}

void IoRuntime_Poll(void)
{
  const uint32_t now = HAL_GetTick();
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
      IoProtocol_Discard(&reader);
      break;
    }
    IoCommand command;
    if (IoProtocol_Feed(&reader, byte, &command)) apply(&command);
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
