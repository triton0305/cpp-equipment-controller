extern "C" {
#include "stepper.h"
#include "device_io.h"
#include "io_runtime.h"
#include "uart_io.h"
#include "main.h"
extern uint32_t stepper_hardware_dropped_reports;
}
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
GPIO_TypeDef fake_gpio[3]{};
static uint32_t tick;
static unsigned writes, reads, tx_calls;
static std::deque<int> rx;
static std::string tx;
static bool tx_ready = true;
uint32_t HAL_GetTick(void) { return tick; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin)
{ return p->input & pin ? GPIO_PIN_SET : GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState value)
{
  // Every coil write must preserve PC4 and all unrelated ports/pins.
  CHECK(p == GPIOC && pin != 0 && (pin & ~15U) == 0);
  ++writes;
  if (value == GPIO_PIN_SET) p->output |= pin;
  else p->output &= ~pin;
}
void UartIo_Init(UART_HandleTypeDef *) {}
int UartIo_TryRead(uint8_t *byte)
{
  ++reads;
  if (rx.empty()) return 0;
  const int v = rx.front(); rx.pop_front();
  if (v < 0) return -1;
  *byte = static_cast<uint8_t>(v); return 1;
}
bool UartIo_TryWrite(uint8_t byte)
{
  ++tx_calls;
  if (!tx_ready) return false;
  tx += static_cast<char>(byte); return true;
}
static unsigned coils() { return GPIOC->output & 15U; }
static void seed()
{
  GPIOA->output = 0xa55a; GPIOB->output = 0x5aa5; GPIOC->output = 0xffff;
}
static void preserved()
{
  CHECK(GPIOA->output == 0xa55a && GPIOB->output == 0x5aa5);
  CHECK((GPIOC->output & 0xfff0) == 0xfff0); // includes DHT11 PC4
}
static void poll(uint32_t now)
{
  writes = 0; Stepper_Poll(now);
  CHECK(writes <= 2); preserved();
}
static void driver_test()
{
  seed(); Stepper_Init(); CHECK(!Stepper_IsBusy() && coils() == 0); preserved();
  // Raw mapping masks high bits, preserves both HIGH and LOW unrelated pins.
  for (uint16_t unrelated : {uint16_t(0xfff0), uint16_t(0)})
    for (unsigned mask = 0; mask < 16; ++mask)
    {
      GPIOC->output = unrelated;
      DeviceIo_SetStepperCoils(static_cast<uint8_t>(0xf0 | mask));
      CHECK(coils() == mask && (GPIOC->output & 0xfff0) == unrelated);
    }
  seed(); Stepper_Init();
  CHECK(Stepper_RequestMove(1, STEPPER_FORWARD, 0, 0) == STEPPER_INVALID);
  CHECK(Stepper_RequestMove(1, static_cast<StepperDirection>(99), 512, 0) == STEPPER_INVALID);
  CHECK(!Stepper_IsBusy() && coils() == 0);
  const unsigned expected[8] = {1, 3, 2, 6, 4, 12, 8, 9};
  for (auto direction : {STEPPER_FORWARD, STEPPER_REVERSE})
  {
    Stepper_Init(); uint32_t now = 17;
    CHECK(Stepper_RequestMove(42, direction, 512, now) == STEPPER_ACCEPTED);
    CHECK(coils() == 0);
    poll(now); CHECK(writes == 0);
    poll(now + 1); CHECK(writes == 0);
    for (unsigned step = 0; step < 512; ++step)
    {
      CHECK(Stepper_RequestMove(42, direction, 512, now) == STEPPER_DUPLICATE);
      CHECK(Stepper_RequestMove(43, direction, 512, now) == STEPPER_BUSY);
      CHECK(Stepper_RequestMove(42, direction, 1, now) == STEPPER_INVALID);
      now += 2; poll(now);
      CHECK(writes > 0 && Stepper_IsBusy());
      CHECK(coils() == expected[direction == STEPPER_FORWARD ? step % 8 : 7 - step % 8]);
      poll(now); CHECK(writes == 0); // same tick cannot step again
      poll(now + 1); CHECK(writes == 0); // phase dwell
    }
    // Exactly 512 phases; release occurs only after the last full dwell.
    poll(now + 2); CHECK(coils() == 0 && !Stepper_IsBusy());
    CHECK(Stepper_RequestMove(42, direction, 512, now + 3) == STEPPER_DUPLICATE);
    poll(now + 100); CHECK(coils() == 0 && writes == 0);
  }
  Stepper_Init();
  CHECK(Stepper_RequestMove(1, STEPPER_FORWARD, 512, 0) == STEPPER_ACCEPTED);
  poll(1000); CHECK(coils() == 1); // late poll advances only one phase
  poll(1000); CHECK(writes == 0);
  poll(1001); CHECK(writes == 0);
  poll(1002); CHECK(coils() == 3);
  Stepper_StopAndRelease(); CHECK(coils() == 0 && !Stepper_IsBusy());
  CHECK(Stepper_RequestMove(1, STEPPER_FORWARD, 512, 1003) == STEPPER_DUPLICATE);
  poll(2000); CHECK(writes == 0);
  CHECK(Stepper_RequestMove(2, STEPPER_REVERSE, 1, 2000) == STEPPER_ACCEPTED);
  poll(2002); CHECK(coils() == 9);
  poll(2004); CHECK(coils() == 0 && !Stepper_IsBusy());
  // Cancellation before first phase and reinitialization while busy.
  CHECK(Stepper_RequestMove(3, STEPPER_FORWARD, 512, 2004) == STEPPER_ACCEPTED);
  Stepper_StopAndRelease(); poll(3000); CHECK(writes == 0);
  CHECK(Stepper_RequestMove(4, STEPPER_FORWARD, 512, 3000) == STEPPER_ACCEPTED);
  poll(3002); Stepper_Init(); CHECK(coils() == 0 && !Stepper_IsBusy());
  poll(4000); CHECK(writes == 0);
  CHECK(Stepper_RequestMove(4, STEPPER_FORWARD, 2, UINT32_MAX - 1) == STEPPER_ACCEPTED);
  poll(UINT32_MAX); CHECK(writes == 0);
  poll(0); CHECK(coils() == 1);
  poll(1); CHECK(writes == 0);
  poll(2); CHECK(coils() == 3);
  poll(4); CHECK(coils() == 0 && !Stepper_IsBusy());
}
static void bench_poll()
{
  reads = tx_calls = 0; IoRuntime_Poll();
  CHECK(reads <= 32 && tx_calls <= 32); preserved();
}
static void send(const std::string &s)
{
  for (unsigned char c : s) rx.push_back(c);
  while (!rx.empty()) bench_poll();
}
static void bench_test()
{
  ADC_HandleTypeDef adc{}; UART_HandleTypeDef uart{};
  seed(); tick = 0; IoRuntime_Init(&adc, &uart);
  for (tick = 0; tick < 5000; ++tick) bench_poll();
  CHECK(coils() == 0 && !Stepper_IsBusy());
  CHECK(tx == "STEPPER BENCH READY COILS_OFF POSITION_UNKNOWN\n");
  send("VALVE:OPEN\nFWDx\n"); CHECK(!Stepper_IsBusy());
  send("FWD"); CHECK(!Stepper_IsBusy());
  send("\r\n"); CHECK(Stepper_IsBusy() && coils() == 0);
  const uint32_t start = tick;
  unsigned phases = 0;
  for (unsigned i = 1; i <= 1026; ++i)
  {
    const unsigned before = coils(); tick = start + i;
    if (i == 200) send("REV\nFWD\n"); // busy requests cannot restart/queue
    bench_poll();
    if (coils() && coils() != before) ++phases;
  }
  CHECK(phases == 512 && !Stepper_IsBusy() && coils() == 0);
  for (unsigned i = 0; i < 10; ++i) bench_poll();
  CHECK(tx.find("START FWD 512\n") != std::string::npos);
  CHECK(tx.find("BUSY\n") != std::string::npos);
  CHECK(tx.find("DONE COILS_OFF\n") != std::string::npos);
  send("REV\n");
  tick += 2; bench_poll(); CHECK(coils() == 9);
  for (unsigned i = 0; i < 512; ++i) { tick += 2; bench_poll(); }
  CHECK(!Stepper_IsBusy() && coils() == 0);
  send("FWD\n"); tick += 2; bench_poll(); CHECK(coils() != 0);
  // STOP is processed before the next due step, despite TX backpressure.
  tx_ready = false; tick += 2; send("STOP\n");
  CHECK(!Stepper_IsBusy() && coils() == 0);
  for (unsigned i = 0; i < 100; ++i) { tick += 2; bench_poll(); }
  CHECK(coils() == 0);
  tx_ready = true; send("REV\n"); tick += 2; bench_poll(); CHECK(coils() == 9);
  rx.push_back(-1); bench_poll(); CHECK(coils() == 0 && !Stepper_IsBusy());
  send("FWD\n"); CHECK(!Stepper_IsBusy()); // error discard through LF
  send(std::string(40, 'x') + "FWD\n"); CHECK(!Stepper_IsBusy());
  send(std::string("FWD\0\n", 5)); CHECK(!Stepper_IsBusy());
  send("FWD\n"); tick += 2; bench_poll(); CHECK(coils() == 1);
  send("STOP\n"); CHECK(coils() == 0);
  for (unsigned i = 0; i < 10; ++i) bench_poll();
  CHECK(stepper_hardware_dropped_reports == 0);
  // Saturated reports cannot block STOP or accidentally start movement.
  tx_ready = false;
  for (unsigned i = 0; i < 40; ++i) send("UNKNOWN\n");
  CHECK(stepper_hardware_dropped_reports > 0);
  send("FWD\n"); tick += 2; bench_poll(); CHECK(coils() == 1);
  send("STOP\n"); CHECK(!Stepper_IsBusy() && coils() == 0);
}
int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "--bench") bench_test();
  else driver_test();
  std::cout << "Stepper host PASS\n";
}
