#include "stepper.h"
#include "device_io.h"

/* Bit 0..3 = IN1..IN4. Forward/reverse are electrical sequence labels only. */
static const uint8_t phases[8] = {0x1, 0x3, 0x2, 0x6, 0x4, 0xc, 0x8, 0x9};
static bool busy, have_request;
static uint32_t last_tick, remaining, request_id, requested_steps;
static StepperDirection requested_direction;
static unsigned phase;

void Stepper_Init(void)
{
  busy = have_request = false;
  remaining = 0;
  phase = 0;
  DeviceIo_SetStepperCoils(0);
}

StepperRequestResult Stepper_RequestMove(uint32_t id, StepperDirection direction,
                                         uint32_t half_steps, uint32_t now)
{
  if ((direction != STEPPER_FORWARD && direction != STEPPER_REVERSE) || !half_steps)
    return STEPPER_INVALID;
  if (have_request && id == request_id)
    return direction == requested_direction && half_steps == requested_steps
      ? STEPPER_DUPLICATE : STEPPER_INVALID;
  if (busy) return STEPPER_BUSY;
  have_request = busy = true;
  request_id = id;
  requested_direction = direction;
  requested_steps = remaining = half_steps;
  phase = direction == STEPPER_FORWARD ? 0U : 7U;
  last_tick = now;
  return STEPPER_ACCEPTED;
}

void Stepper_Poll(uint32_t now)
{
  if (!busy || (uint32_t)(now - last_tick) < STEPPER_INTERVAL_MS) return;
  last_tick = now;
  if (!remaining)
  {
    Stepper_StopAndRelease();
    return;
  }
  DeviceIo_SetStepperCoils(phases[phase]);
  phase = (phase + (requested_direction == STEPPER_FORWARD ? 1U : 7U)) & 7U;
  --remaining;
}

void Stepper_StopAndRelease(void)
{
  busy = false;
  remaining = 0;
  DeviceIo_SetStepperCoils(0);
}

bool Stepper_IsBusy(void) { return busy; }
