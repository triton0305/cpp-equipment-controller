#include "runtime_fixture.hpp"
#include <sstream>

struct EventCapture
{
  std::ostringstream events;
  std::streambuf* previous = std::cerr.rdbuf(events.rdbuf());
  ~EventCapture() { std::cerr.rdbuf(previous); }
  unsigned recoveries() const
  {
    unsigned count = 0;
    const auto text = events.str();
    for (auto pos = text.find("EVENT sensor communication recovered");
         pos != std::string::npos;
         pos = text.find("EVENT sensor communication recovered", pos + 1)) ++count;
    return count;
  }
};

int main()
{
  try
  {
    // Boot is invalid until a fresh boundary arrives;
    // no START before synchronization.
    {
      Fixture f;

      CHECK(!f.core.getSensorState().communication_ok);

      f.core.handleCommand(Command{CommandType::Start});

      CHECK(f.core.getState() == EquipmentState::Idle);
      safe(f.core);

      f.runtime->poll();

      CHECK(
        f.endpoint->drain() ==
        "\n" + safe_packet(false)
      );

      f.at(999);

      CHECK(f.core.getState() == EquipmentState::Idle);

      f.endpoint->drain();

      f.at(1000);

      fault(f.core);

      CHECK(
        f.endpoint->drain() ==
        safe_packet(true)
      );
    }

    // Normal traffic refreshes the deadline;
    // arbitrary/incomplete traffic does not.
    for (const std::string noise :
         {"", "GARBAGE\n", "TEMP:INVALID\n", "TEMP:nan\n", "TEMP:bad\n", "PRESSURE:"})
    {
      Fixture f;

      f.startRunning();

      f.at(100);
      f.receive("PRESSURE:2048\n");
      f.endpoint->drain();

      f.at(1099);

      CHECK(f.core.getState() == EquipmentState::Run);

      f.receive(noise);

      CHECK(f.core.getState() == EquipmentState::Run);

      f.at(1100);

      fault(f.core);

      CHECK(
        f.endpoint->drain() ==
        safe_packet(true)
      );

      f.core.handleCommand(Command{CommandType::Reset});

      fault(f.core);

      // Queue a valid old line before reopening:
      // tcflush must discard it.
      f.endpoint->send("TEMP:77\n");

      f.at(1599);

      CHECK(f.endpoint->drain().empty());

      f.at(1600);

      CHECK(
        f.endpoint->drain() ==
        "\n" + safe_packet(true)
      );

      f.runtime->poll();

      fault(f.core);

      CHECK(
        f.core.getSensorState().temperature == 25
      );

      f.receive("70\n");

      fault(f.core);

      // Old partial PRESSURE must not survive reopen.
      f.receive("DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\n");

      CHECK(
        f.core.getSensorState().communication_ok
      );

      CHECK(
        f.core.getState() ==
        EquipmentState::Error
      );

      safe(f.core);

      CHECK(
        f.endpoint->drain() ==
        error_packet() + error_packet()
      );

      f.core.handleCommand(Command{CommandType::Start});

      CHECK(
        f.core.getState() ==
        EquipmentState::Error
      );

      // Existing reset guard still rejects
      // an unsafe sensor even after recovery.
      f.receive("DOOR:OPEN\n");

      f.core.handleCommand(Command{CommandType::Reset});

      CHECK(
        f.core.getState() ==
        EquipmentState::Error
      );

      f.receive("DOOR:CLOSED\nTEMP:25\nHUMIDITY:45\n");

      f.core.handleCommand(Command{CommandType::Reset});

      CHECK(
        f.core.getState() ==
        EquipmentState::Idle
      );

      safe(f.core);

      CHECK(
        f.core.getSequenceStep() ==
        SequenceStep::None
      );
    }

    // UART disappearance while RUN,
    // device path reappearance, timed reopening.
    {
      Fixture f;

      f.startRunning();

      close(f.endpoint->master);
      f.endpoint->master = -1;

      f.runtime->poll();

      fault(f.core);

      f.attach();

      f.at(499);

      CHECK(
        f.endpoint->drain().empty()
      );

      f.at(500);

      CHECK(
        f.endpoint->drain() ==
        "\n" + safe_packet(true)
      );

      fault(f.core);
      EventCapture capture;
      f.receive("56\nOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nSTART\nRESET\n");
      fault(f.core);
      CHECK(capture.recoveries() == 0);
      CHECK(f.endpoint->drain().empty());
      f.at(600);
      CHECK(f.endpoint->drain() == safe_packet(true));

      f.receive(
        "DOOR:CLOSED\n"
        "ESTOP:OFF\n"
        "PRESSURE:2048\n"
      );

      CHECK(
        f.core.getState() ==
          EquipmentState::Error &&
        f.core.getSensorState().communication_ok
      );

      safe(f.core);
      CHECK(f.core.getAlarmCode() == AlarmCode::CommunicationFault);
      CHECK(capture.recoveries() == 1);

      CHECK(
        f.endpoint->drain() ==
        error_packet() +
        error_packet()
      );
    }

    // First complete boundary is accepted, even across polls. Partial data
    // remains private; every intervening line cancels the pending DOOR.
    for (const std::string interruption :
         {"PRESSURE:2048", "TEMP:25", "garbage", "OOR:CLOSED", "ESTOP:bad", "RESET", ""})
    {
      Fixture f;
      EventCapture capture;
      f.runtime->poll(); f.endpoint->drain();
      const bool old_door = f.core.getSensorState().door_closed;
      f.receive("56\nOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\n");
      CHECK(!f.core.getSensorState().communication_ok);
      f.receive("DOOR:OPEN\n");
      CHECK(f.core.getSensorState().door_closed == old_door);
      CHECK(!f.runtime->handleCommand(Command{CommandType::Start}));
      CHECK(!f.runtime->handleCommand(Command{CommandType::Reset}));
      f.receive(interruption + "\nESTOP:OFF\n");
      CHECK(!f.core.getSensorState().communication_ok);
      CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(capture.recoveries() == 0);
      f.at(100);
      CHECK(f.endpoint->drain() == safe_packet(false));
      f.receive("DOOR:OPEN\nDOOR:CLOSED\n");
      CHECK(!f.core.getSensorState().communication_ok);
      f.receive("ESTOP:OFF\n");
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(f.core.getSensorState().door_closed);
      CHECK(!f.core.getSensorState().emergency_stop);
      CHECK(capture.recoveries() == 1);
      CHECK(!f.core.getSensorState().temperature_valid);
      CHECK(!f.core.getSensorState().humidity_valid);
      CHECK(f.endpoint->drain().find("STATE:READY") == std::string::npos);
      safe(f.core);
    }

    for (const std::string dht : {"", "TEMP:INVALID\nHUMIDITY:INVALID\n"})
    {
      Fixture f; EventCapture capture;
      f.runtime->poll(); f.endpoint->drain();
      f.receive("DOOR:CLOSED\nESTOP:OFF\n" + dht);
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(!f.core.getSensorState().temperature_valid);
      CHECK(!f.core.getSensorState().humidity_valid);
      CHECK(capture.recoveries() == 1);
      f.at(900); f.receive("PRESSURE:2048\n");
      f.at(1000);
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(capture.recoveries() == 1);
      f.at(1899); CHECK(f.core.getSensorState().communication_ok);
      f.at(1900); fault(f.core);
    }

    // Noise never extends the separate open-to-boundary deadline.
    {
      Fixture f; EventCapture capture;
      f.runtime->poll(); f.endpoint->drain();
      for (unsigned t = 100; t < 1000; t += 100)
      {
        f.at(t);
        f.receive("DOOR:CLOSED\nTEMP:25\nESTOP:OFF\nPRESSURE:2048\ngarbage\n");
        CHECK(!f.core.getSensorState().communication_ok);
        CHECK(f.core.getState() == EquipmentState::Idle);
        CHECK(f.endpoint->drain() == safe_packet(false));
      }
      f.at(999); f.receive("DOOR:CLOSED\n");
      f.now += std::chrono::milliseconds(1);
      f.receive("ESTOP:OFF\n");
      fault(f.core);
      CHECK(capture.recoveries() == 0);
      CHECK(f.endpoint->drain() == safe_packet(true));
      f.at(1499); CHECK(f.endpoint->drain().empty());
      f.at(1500); CHECK(f.endpoint->drain() == "\n" + safe_packet(true));
    }

    // Framing loss must be observed in order, including across read batches.
    for (const bool split : {false, true})
    {
      Fixture f; EventCapture capture;
      f.runtime->poll(); f.endpoint->drain();
      if (split)
      {
        f.receive("DOOR:CLOSED\n" + std::string(256, 'X'));
        f.receive("X");
        f.receive("\nESTOP:OFF\n");
      }
      else f.receive("DOOR:CLOSED\n" + std::string(257, 'X') + "\nESTOP:OFF\n");
      CHECK(!f.core.getSensorState().communication_ok);
      CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(capture.recoveries() == 0);
      CHECK(!f.runtime->handleCommand(Command{CommandType::Start}));
      f.at(999);
      CHECK(f.endpoint->drain() == safe_packet(false));
      f.receive(std::string(257, 'X') + "\n");
      f.at(1000); fault(f.core);
      CHECK(capture.recoveries() == 0);
    }
    for (const std::string prefix : {"", "DOOR:CLOSED\n"})
    {
      Fixture f; EventCapture capture;
      f.runtime->poll(); f.endpoint->drain();
      f.receive(prefix + std::string(257, 'X') + "\nDOOR:CLOSED\nESTOP:OFF\n");
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(capture.recoveries() == 1);
      safe(f.core);
    }
    // Operational discard neither faults immediately nor refreshes liveness.
    for (const bool normal_traffic : {false, true})
    {
      Fixture f;
      f.runtime->poll(); f.endpoint->drain();
      f.receive("DOOR:CLOSED\nESTOP:OFF\n");
      f.at(900);
      f.receive(std::string(257, 'X') + "\n");
      CHECK(f.core.getSensorState().communication_ok);
      if (normal_traffic) f.receive("PRESSURE:2048\n");
      f.at(999); CHECK(f.core.getSensorState().communication_ok);
      f.at(1000);
      if (normal_traffic)
      {
        CHECK(f.core.getSensorState().communication_ok);
        f.at(1899); CHECK(f.core.getSensorState().communication_ok);
        f.at(1900);
      }
      fault(f.core);
    }

    // Cold boot safety is evaluated immediately on the completing ESTOP.
    {
      Fixture f;
      f.runtime->poll(); f.endpoint->drain();
      f.receive("DOOR:CLOSED\nESTOP:ON\n");
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
      safe(f.core);
    }

    // B1 packet can reset after recovery in the same RX batch, then must
    // qualify READY for a new full five seconds.
    {
      Fixture f; f.startRunning();
      close(f.endpoint->master); f.endpoint->master = -1;
      f.runtime->poll(); fault(f.core);
      f.attach(); f.at(500); f.endpoint->drain();
      f.receive("RESET\nDOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25\nHUMIDITY:45\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(f.core.getSensorState().communication_ok);
      CHECK(f.endpoint->drain().find("STATE:READY") == std::string::npos);
      CHECK(!f.runtime->handleCommand(Command{CommandType::Start}));
      for (unsigned t = 600; t < 5500; t += 100)
      {
        f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(t);
        f.receive("PRESSURE:2048\nTEMP:25\nHUMIDITY:45\n");
        CHECK(f.endpoint->drain().find("STATE:READY") == std::string::npos);
      }
      f.at(5499); CHECK(f.endpoint->drain().find("STATE:READY") == std::string::npos);
      f.at(5500); CHECK(f.endpoint->drain().find("STATE:READY") != std::string::npos);
      safe(f.core);
    }

    // Real runtime write-failure branch:
    // transport write returns EIO deterministically.
    {
      Fixture f;

      f.startRunning();

      f.endpoint->send(
        "PRESSURE:2048\n"
        "DOOR:CLOSED\n"
      );

      fail_write = true;

      f.runtime->poll();

      CHECK(!fail_write);

      fault(f.core);

      CHECK(
        f.endpoint->drain().empty()
      );

      f.at(500);

      CHECK(
        f.endpoint->drain() ==
        "\n" + safe_packet(true)
      );

      f.runtime->poll();

      fault(f.core);
      // Remaining old batch was not consumed.
    }

    // A delayed successful write must not rejuvenate
    // the rest of an old RX batch.
    {
      Fixture f;

      f.startRunning();

      const auto active =
        AsciiProtocol::serializeDeviceState(
          f.core.getDeviceState()
        ) +
        "STATE:RUN\n";

      f.endpoint->send(
        "PRESSURE:2048\n"
        "DOOR:OPEN\n"
      );

      delayed_write_clock = &f.now;

      f.runtime->poll();

      CHECK(
        delayed_write_clock == nullptr
      );

      fault(f.core);

      CHECK(
        f.core.getSensorState().door_closed
      );

      // Old second line was discarded.
      CHECK(
        f.endpoint->drain() ==
        active + safe_packet(true)
      );
    }

    // Missing port at startup retries
    // without clearing ERROR on a later open.
    {
      Fixture f(false);

      f.runtime->poll();

      fault(f.core);

      f.attach();

      f.at(499);

      CHECK(
        f.endpoint->drain().empty()
      );

      f.at(500);

      CHECK(
        f.endpoint->drain() ==
        "\n" + safe_packet(true)
      );

      fault(f.core);
    }

    // Preserve an already-latched fault
    // rather than replace its alarm reason.
    {
      Fixture f;

      f.startRunning();

      f.receive("ESTOP:ON\n");

      CHECK(
        f.core.getAlarmCode() ==
        AlarmCode::EmergencyStop
      );

      f.at(1000);

      CHECK(
        f.core.getAlarmCode() ==
        AlarmCode::EmergencyStop
      );

      CHECK(
        !f.core.getSensorState().communication_ok
      );

      safe(f.core);
    }

    // Graceful shutdown while RUN
    // transmits a safe complete refresh.
    {
      Fixture f;

      f.startRunning();

      f.runtime->shutdown();

      safe(f.core);

      CHECK(
        f.endpoint->drain() ==
        safe_packet(false)
      );
    }

    std::cout
      << "Linux runtime recovery Host PASS\n";
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << '\n';
    return 1;
  }
}