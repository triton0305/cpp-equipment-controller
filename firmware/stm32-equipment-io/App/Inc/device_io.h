#ifndef DEVICE_IO_H
#define DEVICE_IO_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  DEVICE_STATE_IDLE,
  DEVICE_STATE_READY,
  DEVICE_STATE_RUN,
  DEVICE_STATE_ERROR
} DeviceStateLed;

bool DeviceIo_ReadDoorActive(void);
bool DeviceIo_ReadEmergencyStopActive(void);

void DeviceIo_SetPump(bool on);
void DeviceIo_SetHeater(bool on);
void DeviceIo_SetBuzzer(bool on);
void DeviceIo_SetStateLed(DeviceStateLed state);

/* Direct Linux RUN_LED / ERROR_LED outputs: green / red; blue off. */
void DeviceIo_SetIndicators(bool run, bool error);

void DeviceIo_SetSafeState(void);

/* Low four bits map to IN1..IN4 (PC0..PC3); all other pins are preserved. */
void DeviceIo_SetStepperCoils(uint8_t pattern);

#endif
