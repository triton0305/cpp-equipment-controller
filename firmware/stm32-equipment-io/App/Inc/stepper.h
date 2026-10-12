#ifndef STEPPER_H
#define STEPPER_H

#include <stdbool.h>
#include <stdint.h>

#define STEPPER_INTERVAL_MS 2U
#define STEPPER_TEST_HALF_STEPS 512U

typedef enum { STEPPER_FORWARD, STEPPER_REVERSE } StepperDirection;
typedef enum {
  STEPPER_ACCEPTED, STEPPER_DUPLICATE, STEPPER_BUSY, STEPPER_INVALID
} StepperRequestResult;

/* Single foreground owner. No absolute position or OPEN/CLOSED claim: position
   remains UNKNOWN. Init releases the coils and clears request history. */
void Stepper_Init(void);
/* Local request identity (not a wire protocol/session number). Retrying the last
   accepted id with identical parameters is a no-op, even after completion/stop.
   Reusing that id with different parameters is invalid. A new move needs a new
   id; while busy, new requests are rejected rather than queued. */
StepperRequestResult Stepper_RequestMove(uint32_t id, StepperDirection direction,
                                         uint32_t half_steps, uint32_t now);
/* At most one phase per call. Last phase receives a full interval before release.
   Late polls schedule from now, never burst to catch up. */
void Stepper_Poll(uint32_t now);
void Stepper_StopAndRelease(void);
bool Stepper_IsBusy(void);

#endif
