#include "device_io.h"

#include "main.h"

bool DeviceIo_ReadDoorActive(void)
{
  return HAL_GPIO_ReadPin(DOOR_SW_GPIO_Port, DOOR_SW_Pin) == GPIO_PIN_RESET;
}

bool DeviceIo_ReadEmergencyStopActive(void)
{
  return HAL_GPIO_ReadPin(ESTOP_SW_GPIO_Port, ESTOP_SW_Pin) == GPIO_PIN_RESET;
}

void DeviceIo_SetPump(bool on)
{
  HAL_GPIO_WritePin(
    PUMP_LED_GPIO_Port,
    PUMP_LED_Pin,
    on ? GPIO_PIN_SET : GPIO_PIN_RESET
  );
}

void DeviceIo_SetHeater(bool on)
{
  HAL_GPIO_WritePin(
    HEATER_LED_GPIO_Port,
    HEATER_LED_Pin,
    on ? GPIO_PIN_SET : GPIO_PIN_RESET
  );
}

void DeviceIo_SetBuzzer(bool on)
{
  HAL_GPIO_WritePin(
    ALARM_BUZZER_GPIO_Port,
    ALARM_BUZZER_Pin,
    on ? GPIO_PIN_SET : GPIO_PIN_RESET
  );
}

void DeviceIo_SetStateLed(DeviceStateLed state)
{
  HAL_GPIO_WritePin(STATE_R_GPIO_Port, STATE_R_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATE_G_GPIO_Port, STATE_G_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATE_B_GPIO_Port, STATE_B_Pin, GPIO_PIN_RESET);

  switch (state)
  {
    case DEVICE_STATE_IDLE:
      HAL_GPIO_WritePin(
        STATE_B_GPIO_Port,
        STATE_B_Pin,
        GPIO_PIN_SET
      );
      break;

    case DEVICE_STATE_READY:
      HAL_GPIO_WritePin(
        STATE_R_GPIO_Port,
        STATE_R_Pin,
        GPIO_PIN_SET
      );
      HAL_GPIO_WritePin(
        STATE_G_GPIO_Port,
        STATE_G_Pin,
        GPIO_PIN_SET
      );
      break;

    case DEVICE_STATE_RUN:
      HAL_GPIO_WritePin(
        STATE_G_GPIO_Port,
        STATE_G_Pin,
        GPIO_PIN_SET
      );
      break;

    case DEVICE_STATE_ERROR:
      HAL_GPIO_WritePin(
        STATE_R_GPIO_Port,
        STATE_R_Pin,
        GPIO_PIN_SET
      );
      break;

    default:
      break;
  }
}

void DeviceIo_SetSafeState(void)
{
  DeviceIo_SetPump(false);
  DeviceIo_SetHeater(false);
  DeviceIo_SetBuzzer(false);

  HAL_GPIO_WritePin(STATE_R_GPIO_Port, STATE_R_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATE_G_GPIO_Port, STATE_G_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATE_B_GPIO_Port, STATE_B_Pin, GPIO_PIN_RESET);

  HAL_GPIO_WritePin(
    STEPPER_IN1_GPIO_Port,
    STEPPER_IN1_Pin,
    GPIO_PIN_RESET
  );
  HAL_GPIO_WritePin(
    STEPPER_IN2_GPIO_Port,
    STEPPER_IN2_Pin,
    GPIO_PIN_RESET
  );
  HAL_GPIO_WritePin(
    STEPPER_IN3_GPIO_Port,
    STEPPER_IN3_Pin,
    GPIO_PIN_RESET
  );
  HAL_GPIO_WritePin(
    STEPPER_IN4_GPIO_Port,
    STEPPER_IN4_Pin,
    GPIO_PIN_RESET
  );
}
