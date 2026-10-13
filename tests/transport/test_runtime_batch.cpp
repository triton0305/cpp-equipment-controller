#include "runtime_fixture.hpp"
#include <vector>

static const std::string normal =
    "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:1500\nTEMP:25\nHUMIDITY:45\nDOOR:CLOSED\nESTOP:OFF\n";

// Observe actual transport writes and advance the injected clock, never sleep.
struct Writes
{
  std::vector<std::string> packets;
  explicit Writes(Fixture& f, unsigned delay_ms = 0)
  {
    observe_write = [this, &f, delay_ms](std::string_view packet) {
      packets.emplace_back(packet);
      f.now += std::chrono::milliseconds(delay_ms);
    };
  }
  ~Writes() { observe_write = {}; }
};

static void open(Fixture& f)
{
  f.runtime->poll(); f.endpoint->drain();
}

static void qualify(Fixture& f)
{
  open(f);
  for (unsigned t = 0; t <= 5000; t += 100)
  {
    f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(t);
    f.receive(normal); f.endpoint->drain();
  }
}

static void batch(unsigned delay_ms)
{
  Fixture f; open(f);
  Writes writes(f, delay_ms);
  const auto received = f.now;
  f.receive(normal);
  CHECK(writes.packets.size() == 1);
  CHECK(writes.packets[0] == AsciiProtocol::serializeDeviceState(DeviceState{}) + "STATE:IDLE\n");
  CHECK(f.now - received == std::chrono::milliseconds(delay_ms));
  CHECK(f.core.getSensorState().communication_ok);
  CHECK(f.core.getAlarmCode() == AlarmCode::None);
  // A second unchanged batch still refreshes the MCU command-loss watchdog.
  f.receive(normal);
  CHECK(writes.packets.size() == 2);
  CHECK(f.now - received == std::chrono::milliseconds(2 * delay_ms));
}

static void timestamp()
{
  Fixture f; qualify(f);
  Writes writes(f, 110);
  const auto received = f.now;
  // START transmits before later sensors in the SAME old batch are consumed.
  f.receive("PRESSURE:1500\nSTART\nTEMP:26\nHUMIDITY:46\n");
  CHECK(writes.packets.size() == 1);
  CHECK(f.now - received == std::chrono::milliseconds(110));
  CHECK(f.core.getSensorState().temperature == 26);
  CHECK(f.core.getSensorState().humidity == 46);
  f.now = received + ControllerRuntime::SensorTimeout - std::chrono::milliseconds(1);
  f.runtime->poll(); CHECK(f.core.getSensorState().communication_ok);
  f.now = received + ControllerRuntime::SensorTimeout;
  f.runtime->poll(); fault(f.core); // Deadline is receive time, not delayed write time.
}

static void safety()
{
  const struct { const char* line; AlarmCode alarm; } cases[] = {
    {"DOOR:OPEN\n", AlarmCode::DoorOpen},
    {"ESTOP:ON\n", AlarmCode::EmergencyStop},
    {"TEMP:51\nTEMP:51\n", AlarmCode::TemperatureCritical},
    {"PRESSURE:3901\nPRESSURE:3901\n", AlarmCode::PressureCritical},
    {"HUMIDITY:91\nHUMIDITY:91\n", AlarmCode::HumidityCritical}
  };
  for (const auto& c : cases)
  {
    Fixture f; f.startRunning();
    unsigned writes = 0;
    observe_write = [&](std::string_view packet) {
      ++writes;
      CHECK(f.core.getAlarmCode() == c.alarm);
      safe(f.core);
      CHECK(packet == error_packet());
      // Tail marker must not yet have been consumed at the safe write.
      CHECK(f.core.getSensorState().temperature != 26);
    };
    f.receive(std::string("PRESSURE:1500\n") + c.line + "TEMP:26\n");
    observe_write = {};
    CHECK(writes == 1);
    CHECK(f.core.getSensorState().temperature == 26);
    CHECK(f.core.getAlarmCode() == c.alarm);
  }
}

static void commands()
{
  Fixture f; qualify(f);
  Writes writes(f);
  f.receive("PRESSURE:1500\nSTART\nTEMP:25\n");
  CHECK(f.core.getState() == EquipmentState::Run);
  DeviceState running; running.run_led = true;
  CHECK(writes.packets.size() == 1);
  CHECK(writes.packets.back() == AsciiProtocol::serializeDeviceState(running) + "STATE:RUN\n");
  f.receive("PRESSURE:1500\nSTOP\nTEMP:25\n");
  CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
  const auto idle = AsciiProtocol::serializeDeviceState(DeviceState{}) + "STATE:IDLE\n";
  CHECK(writes.packets.size() == 2 && writes.packets.back() == idle);
  f.receive("ESTOP:ON\n");
  CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
  CHECK(writes.packets.size() == 3 && writes.packets.back() == error_packet());
  f.receive("ESTOP:OFF\nRESET\nTEMP:25\n");
  CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
  CHECK(f.core.getAlarmCode() == AlarmCode::None);
  CHECK(writes.packets.size() == 4 && writes.packets.back() == idle);
  // Rejected START must not erase a pending normal refresh or bypass READY.
  f.receive("TEMP:25\nSTART\n");
  CHECK(f.core.getState() == EquipmentState::Idle);
  CHECK(writes.packets.size() == 5 && writes.packets.back() == idle);
}

int main(int argc, char** argv)
{
  try
  {
    CHECK(argc == 2);
    const std::string test = argv[1];
    if (test == "batch") batch(0);
    else if (test == "delay") batch(110);
    else if (test == "timestamp") timestamp();
    else if (test == "safety") safety();
    else if (test == "commands") commands();
    else CHECK(false);
    std::cout << "UART batch " << test << " PASS\n";
  }
  catch (const std::exception& e)
  { observe_write = {}; std::cerr << e.what() << '\n'; return 1; }
}
