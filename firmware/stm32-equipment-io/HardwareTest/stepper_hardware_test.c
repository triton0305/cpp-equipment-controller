/* Bench-only replacement for io_runtime.c. The unchanged generated main calls
   these entry adapters ONLY in the explicit StepperHardware build. No production
   parser, equipment control, or communication watchdog runs in this firmware. */
#include "io_runtime.h"
#include "stepper.h"
#include "uart_io.h"
#include <string.h>

static char line[16];
static unsigned length;
static bool discard;
static uint32_t next_id;
static char tx[256];
static unsigned head, tail;
uint32_t stepper_hardware_dropped_reports;

static void report(const char *message)
{
  const unsigned n = (unsigned)strlen(message);
  const unsigned free_space = (tail + sizeof(tx) - head - 1U) % sizeof(tx);
  if (n > free_space) { ++stepper_hardware_dropped_reports; return; }
  for (unsigned i = 0; i < n; ++i)
  {
    tx[head] = message[i];
    head = (head + 1U) % sizeof(tx);
  }
}

static void command(uint32_t now)
{
  if (!strcmp(line, "STOP"))
  {
    Stepper_StopAndRelease();
    report("STOPPED COILS_OFF\n");
  }
  else if (!strcmp(line, "FWD") || !strcmp(line, "REV"))
  {
    /* Busy repeats never become new movement, including the final dwell. */
    if (Stepper_IsBusy()) { report("BUSY\n"); return; }
    const bool forward = line[0] == 'F';
    const StepperRequestResult result = Stepper_RequestMove(++next_id,
      forward ? STEPPER_FORWARD : STEPPER_REVERSE, STEPPER_TEST_HALF_STEPS, now);
    if (result == STEPPER_ACCEPTED)
      report(forward ? "START FWD 512\n" : "START REV 512\n");
    else report("REJECTED\n");
  }
  else report("UNKNOWN_COMMAND\n");
}

void IoRuntime_Init(ADC_HandleTypeDef *adc, UART_HandleTypeDef *uart)
{
  (void)adc;
  length = head = tail = 0;
  next_id = stepper_hardware_dropped_reports = 0;
  discard = false;
  Stepper_Init();
  UartIo_Init(uart);
  report("STEPPER BENCH READY COILS_OFF POSITION_UNKNOWN\n");
}

void IoRuntime_Poll(void)
{
  const uint32_t now = HAL_GetTick();
  for (unsigned i = 0; i < 32; ++i)
  {
    uint8_t byte;
    const int result = UartIo_TryRead(&byte);
    if (!result) break;
    if (result < 0)
    {
      length = 0; discard = true;
      Stepper_StopAndRelease();
      report("RX_ERROR COILS_OFF\n");
      break;
    }
    if (byte == '\n')
    {
      if (!discard)
      {
        if (length && line[length - 1U] == '\r') --length;
        line[length] = '\0';
        command(now);
      }
      length = 0; discard = false;
    }
    else if (!discard)
    {
      if (byte == 0 || byte > 126 || (byte < 32 && byte != '\r') ||
          length == sizeof(line) - 1U)
      { length = 0; discard = true; }
      else line[length++] = (char)byte;
    }
  }
  const bool was_busy = Stepper_IsBusy();
  Stepper_Poll(now);
  if (was_busy && !Stepper_IsBusy()) report("DONE COILS_OFF\n");
  for (unsigned i = 0; i < 32 && tail != head; ++i)
  {
    if (!UartIo_TryWrite((uint8_t)tx[tail])) break;
    tail = (tail + 1U) % sizeof(tx);
  }
}
