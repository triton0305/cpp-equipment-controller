#include "controller_runtime.hpp"
#include "ascii_protocol.hpp"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <pty.h>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x); } while (0)
static bool fail_write;
static ControllerRuntime::Clock::time_point* delayed_write_clock;
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size)
{
  if (fail_write) { fail_write = false; errno = EIO; return -1; }
  if (delayed_write_clock)
  {
    *delayed_write_clock += ControllerRuntime::SensorTimeout;
    delayed_write_clock = nullptr;
  }
  return __real_write(fd, data, size);
}
struct Endpoint
{
  int master = -1, slave = -1;
  std::string name;
  Endpoint()
  {
    CHECK(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);
    name = ttyname(slave);
    termios settings{}; CHECK(tcgetattr(slave, &settings) == 0);
    cfmakeraw(&settings); CHECK(tcsetattr(slave, TCSANOW, &settings) == 0);
    CHECK(fcntl(master, F_SETFL, O_NONBLOCK) == 0);
  }
  ~Endpoint() { if (master >= 0) close(master); if (slave >= 0) close(slave); }
  void send(const std::string& text) { CHECK(__real_write(master, text.data(), text.size()) == static_cast<ssize_t>(text.size())); }
  std::string drain()
  {
    std::string result; char buffer[4096]; ssize_t n;
    while ((n = read(master, buffer, sizeof(buffer))) > 0) result.append(buffer, n);
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    return result;
  }
};
struct Fixture
{
  std::string directory, path;
  std::unique_ptr<Endpoint> endpoint;
  EquipmentController core;
  ControllerRuntime::Clock::time_point now{};
  std::unique_ptr<ControllerRuntime> runtime;
  explicit Fixture(bool available = true)
  {
    char pattern[] = "/tmp/controller-recovery-XXXXXX";
    const auto made = mkdtemp(pattern); CHECK(made);
    directory = made; path = directory + "/uart";
    if (available) attach();
    runtime = std::make_unique<ControllerRuntime>(path, core, [this] { return now; });
  }
  ~Fixture() { runtime.reset(); endpoint.reset(); unlink(path.c_str()); rmdir(directory.c_str()); }
  void attach()
  {
    endpoint = std::make_unique<Endpoint>();
    unlink(path.c_str()); CHECK(symlink(endpoint->name.c_str(), path.c_str()) == 0);
  }
  void at(unsigned milliseconds) { now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(milliseconds); runtime->poll(); }
  void receive(const std::string& line) { endpoint->send(line); runtime->poll(); }
  void startRunning()
  {
    runtime->poll(); endpoint->drain();
    receive("PRESSURE:70\nDOOR:CLOSED\nESTOP:OFF\n"); endpoint->drain();
    CHECK(core.getSensorState().communication_ok);
    core.handleCommand(Command{CommandType::Start});
    for (unsigned i = 0; i < 6; ++i) core.update();
    CHECK(core.getState() == EquipmentState::Run && core.getDeviceState().pump_on);
  }
};
static std::string safe_packet(bool alarm)
{
  DeviceState d; d.buzzer_on = d.error_led = alarm;
  return AsciiProtocol::serializeDeviceState(d);
}
static void safe(const EquipmentController& core)
{
  const auto& d = core.getDeviceState();
  CHECK(!d.pump_on && !d.heater_on && !d.valve_open && !d.run_led);
}
static void fault(const EquipmentController& core)
{
  CHECK(core.getState() == EquipmentState::Error);
  CHECK(core.getAlarmCode() == AlarmCode::CommunicationFault);
  CHECK(!core.getSensorState().communication_ok);
  CHECK(core.getDeviceState().error_led && core.getDeviceState().buzzer_on);
  safe(core);
}
int main()
{
  try
  {
    // Boot is invalid until a parsed line arrives; no START before synchronization.
    {
      Fixture f;
      CHECK(!f.core.getSensorState().communication_ok);
      f.core.handleCommand(Command{CommandType::Start});
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      f.runtime->poll(); CHECK(f.endpoint->drain() == "\n" + safe_packet(false));
      f.at(999); CHECK(f.core.getState() == EquipmentState::Idle);
      f.endpoint->drain(); f.at(1000); fault(f.core);
      CHECK(f.endpoint->drain() == safe_packet(true));
    }
    // Normal traffic refreshes the deadline; arbitrary/incomplete traffic does not.
    for (const std::string noise : {"", "GARBAGE\n", "TEMP:nan\n", "TEMP:bad\n", "PRESSURE:"})
    {
      Fixture f; f.startRunning();
      f.at(100); f.receive("PRESSURE:70\n"); f.endpoint->drain();
      f.at(1099); CHECK(f.core.getState() == EquipmentState::Run);
      f.receive(noise); CHECK(f.core.getState() == EquipmentState::Run);
      f.at(1100); fault(f.core); CHECK(f.endpoint->drain() == safe_packet(true));
      f.core.handleCommand(Command{CommandType::Reset}); fault(f.core);
      // Queue a valid old line before reopening: tcflush must discard it.
      f.endpoint->send("TEMP:77\n");
      f.at(1599); CHECK(f.endpoint->drain().empty());
      f.at(1600); CHECK(f.endpoint->drain() == "\n" + safe_packet(true));
      f.runtime->poll(); fault(f.core); CHECK(f.core.getSensorState().temperature == 25);
      f.receive("70\n"); fault(f.core); // Old partial PRESSURE must not survive reopen.
      f.receive("PRESSURE:70\n");
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(f.core.getState() == EquipmentState::Error); safe(f.core);
      CHECK(f.endpoint->drain() == safe_packet(true));
      f.core.handleCommand(Command{CommandType::Start}); CHECK(f.core.getState() == EquipmentState::Error);
      // Existing reset guard still rejects an unsafe sensor even after recovery.
      f.receive("DOOR:OPEN\n");
      f.core.handleCommand(Command{CommandType::Reset}); CHECK(f.core.getState() == EquipmentState::Error);
      f.receive("DOOR:CLOSED\n");
      f.core.handleCommand(Command{CommandType::Reset}); CHECK(f.core.getState() == EquipmentState::Idle);
      safe(f.core); CHECK(f.core.getSequenceStep() == SequenceStep::None);
    }
    // UART disappearance while RUN, device path reappearance, timed reopening.
    {
      Fixture f; f.startRunning();
      close(f.endpoint->master); f.endpoint->master = -1;
      f.runtime->poll(); fault(f.core);
      f.attach(); f.at(499); CHECK(f.endpoint->drain().empty());
      f.at(500); CHECK(f.endpoint->drain() == "\n" + safe_packet(true)); fault(f.core);
      f.receive("DOOR:CLOSED\nESTOP:OFF\nPRESSURE:70\n");
      CHECK(f.core.getState() == EquipmentState::Error && f.core.getSensorState().communication_ok);
      safe(f.core);
      CHECK(f.endpoint->drain() == safe_packet(true) + safe_packet(true) + safe_packet(true));
    }
    // Real runtime write-failure branch: transport write returns EIO deterministically.
    {
      Fixture f; f.startRunning();
      f.endpoint->send("PRESSURE:70\nDOOR:CLOSED\n"); fail_write = true;
      f.runtime->poll(); CHECK(!fail_write); fault(f.core);
      CHECK(f.endpoint->drain().empty());
      f.at(500); CHECK(f.endpoint->drain() == "\n" + safe_packet(true));
      f.runtime->poll(); fault(f.core); // Remaining old batch was not consumed.
    }
    // A delayed successful write must not rejuvenate the rest of an old RX batch.
    {
      Fixture f; f.startRunning();
      const auto active = AsciiProtocol::serializeDeviceState(f.core.getDeviceState());
      f.endpoint->send("PRESSURE:70\nDOOR:OPEN\n"); delayed_write_clock = &f.now;
      f.runtime->poll(); CHECK(delayed_write_clock == nullptr); fault(f.core);
      CHECK(f.core.getSensorState().door_closed); // Old second line was discarded.
      CHECK(f.endpoint->drain() == active + safe_packet(true));
    }
    // Missing port at startup retries without clearing ERROR on a later open.
    {
      Fixture f(false); f.runtime->poll(); fault(f.core);
      f.attach(); f.at(499); CHECK(f.endpoint->drain().empty());
      f.at(500); CHECK(f.endpoint->drain() == "\n" + safe_packet(true)); fault(f.core);
    }
    // Preserve an already-latched fault rather than replace its alarm reason.
    {
      Fixture f; f.startRunning(); f.receive("ESTOP:ON\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
      f.at(1000); CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
      CHECK(!f.core.getSensorState().communication_ok); safe(f.core);
    }
    // Graceful shutdown while RUN transmits a safe complete refresh.
    {
      Fixture f; f.startRunning(); f.runtime->shutdown(); safe(f.core);
      CHECK(f.endpoint->drain() == safe_packet(false));
    }
    std::cout << "Linux runtime recovery Host PASS\n";
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
