#include "runtime_fixture.hpp"

int main()
{
  try
  {
    // Boot is invalid until a parsed line arrives;
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
         {"", "GARBAGE\n", "TEMP:nan\n", "TEMP:bad\n", "PRESSURE:"})
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
      f.receive("PRESSURE:2048\n");

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
        error_packet()
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

      CHECK(
        f.endpoint->drain() ==
        error_packet() +
        error_packet() +
        error_packet()
      );
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