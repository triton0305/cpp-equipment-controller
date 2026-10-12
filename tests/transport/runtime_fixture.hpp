#pragma once
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

#define CHECK(x) \
  do \
  { \
    if (!(x)) \
      throw std::runtime_error( \
        std::string("line ") + std::to_string(__LINE__) + ": " + #x); \
  } while (0)

static bool fail_write;
static ControllerRuntime::Clock::time_point* delayed_write_clock;

extern "C" ssize_t __real_write(int, const void*, size_t);

extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size)
{
  if (fail_write)
  {
    fail_write = false;
    errno = EIO;
    return -1;
  }

  if (delayed_write_clock)
  {
    *delayed_write_clock += ControllerRuntime::SensorTimeout;
    delayed_write_clock = nullptr;
  }

  return __real_write(fd, data, size);
}

struct Endpoint
{
  int master = -1;
  int slave = -1;
  std::string name;

  Endpoint()
  {
    CHECK(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);

    name = ttyname(slave);

    termios settings{};
    CHECK(tcgetattr(slave, &settings) == 0);

    cfmakeraw(&settings);
    CHECK(tcsetattr(slave, TCSANOW, &settings) == 0);

    CHECK(fcntl(master, F_SETFL, O_NONBLOCK) == 0);
  }

  ~Endpoint()
  {
    if (master >= 0)
      close(master);

    if (slave >= 0)
      close(slave);
  }

  void send(const std::string& text)
  {
    CHECK(
      __real_write(master, text.data(), text.size()) ==
      static_cast<ssize_t>(text.size())
    );
  }

  std::string drain()
  {
    std::string result;
    char buffer[4096];
    ssize_t n;

    while ((n = read(master, buffer, sizeof(buffer))) > 0)
    {
      result.append(buffer, n);
    }

    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

    return result;
  }
};

struct Fixture
{
  std::string directory;
  std::string path;

  std::unique_ptr<Endpoint> endpoint;

  EquipmentController core;

  ControllerRuntime::Clock::time_point now{};

  std::unique_ptr<ControllerRuntime> runtime;

  explicit Fixture(bool available = true)
  {
    char pattern[] = "/tmp/controller-recovery-XXXXXX";

    const auto made = mkdtemp(pattern);
    CHECK(made);

    directory = made;
    path = directory + "/uart";

    if (available)
      attach();

    runtime = std::make_unique<ControllerRuntime>(
      path,
      core,
      [this]
      {
        return now;
      }
    );
  }

  ~Fixture()
  {
    runtime.reset();
    endpoint.reset();

    unlink(path.c_str());
    rmdir(directory.c_str());
  }

  void attach()
  {
    endpoint = std::make_unique<Endpoint>();

    unlink(path.c_str());

    CHECK(
      symlink(endpoint->name.c_str(), path.c_str()) == 0
    );
  }

  void at(unsigned milliseconds)
  {
    now =
      ControllerRuntime::Clock::time_point{} +
      std::chrono::milliseconds(milliseconds);

    runtime->poll();
  }

  void receive(const std::string& line)
  {
    endpoint->send(line);
    runtime->poll();
  }

  void startRunning()
  {
    runtime->poll();
    endpoint->drain();

    receive("PRESSURE:2048\nDOOR:CLOSED\nESTOP:OFF\nTEMP:25\nHUMIDITY:45\n");
    endpoint->drain();

    CHECK(core.getSensorState().communication_ok);

    core.handleCommand(Command{CommandType::Start});

    for (unsigned i = 0; i < 6; ++i)
    {
      core.update();
    }

    CHECK(
      core.getState() == EquipmentState::Run &&
      core.getDeviceState().pump_on
    );
  }
};

[[maybe_unused]] static std::string safe_packet(bool alarm)
{
  DeviceState d;

  d.buzzer_on = alarm;
  d.error_led = alarm;

  return
    AsciiProtocol::serializeDeviceState(d) +
    "STATE:SAFE\n";
}

[[maybe_unused]] static std::string error_packet()
{
  DeviceState d;

  d.buzzer_on = true;
  d.error_led = true;

  return
    AsciiProtocol::serializeDeviceState(d) +
    "STATE:ERROR\n";
}

static void safe(const EquipmentController& core)
{
  const auto& d = core.getDeviceState();

  CHECK(!d.pump_on);
  CHECK(!d.heater_on);
  CHECK(!d.valve_open);
  CHECK(!d.run_led);
}

[[maybe_unused]] static void fault(const EquipmentController& core)
{
  CHECK(core.getState() == EquipmentState::Error);
  CHECK(core.getAlarmCode() == AlarmCode::CommunicationFault);
  CHECK(!core.getSensorState().communication_ok);

  CHECK(
    core.getDeviceState().error_led &&
    core.getDeviceState().buzzer_on
  );

  safe(core);
}

