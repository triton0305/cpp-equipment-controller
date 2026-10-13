#include "runtime_fixture.hpp"
#include <sstream>

static std::string sample(Fixture& f, unsigned ms, const std::string& data = "PRESSURE:2048\nTEMP:25\nHUMIDITY:45\n")
{
  f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(ms);
  f.receive(data);
  return f.endpoint->drain();
}
static void heartbeat(Fixture& f, unsigned from, unsigned until)
{
  for (unsigned t = from; t < until; t += 100) sample(f, t);
}
static void boot(Fixture& f)
{
  f.runtime->poll(); f.endpoint->drain();
  sample(f, 0, "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25\nHUMIDITY:45\n");
}
int main()
{
  try
  {
    {
      Fixture f; boot(f);
      heartbeat(f, 100, 4901);
      CHECK(sample(f, 4999).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 5000).find("STATE:READY") != std::string::npos);
      safe(f.core); CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(sample(f, 5010, "DOOR:OPEN\n").find("STATE:IDLE") != std::string::npos);
      sample(f, 5020, "DOOR:CLOSED\n");
      heartbeat(f, 5100, 10001);
      CHECK(sample(f, 10019).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 10020).find("STATE:READY") != std::string::npos);
      sample(f, 10030, "ESTOP:ON\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop); safe(f.core);
    }
    {
      Fixture f; f.runtime->poll(); f.endpoint->drain();
      heartbeat(f, 0, 6001);
      CHECK(sample(f, 6010).find("STATE:READY") == std::string::npos);
      sample(f, 6020, "DOOR:CLOSED\n"); heartbeat(f, 6100, 12001);
      CHECK(sample(f, 12010).find("STATE:READY") == std::string::npos);
    }
    {
      Fixture f; boot(f); heartbeat(f, 100, 4901);
      f.at(5900); fault(f.core); f.endpoint->drain();
      f.at(6400); f.endpoint->drain();
      sample(f, 6400, "PRESSURE:2048\nTEMP:25\nHUMIDITY:45\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      CHECK(!f.core.getSensorState().communication_ok);
      sample(f, 6410, "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25\nHUMIDITY:45\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Idle);
      heartbeat(f, 6500, 11401);
      CHECK(sample(f, 11409).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 11410).find("STATE:READY") != std::string::npos);
    }
    {
      Fixture f; boot(f);
      sample(f, 0, "START\n"); safe(f.core);
      CHECK(f.core.getState() == EquipmentState::Idle);
      heartbeat(f, 100, 5000);
      sample(f, 4999, "START\n"); safe(f.core);
      sample(f, 5000, "START\n");
      CHECK(f.core.getState() == EquipmentState::Ready);
      CHECK(f.core.getSequenceStep() == SequenceStep::CheckDoor);
      safe(f.core);
      sample(f, 5010); CHECK(f.core.getSequenceStep() == SequenceStep::PumpOn);
      sample(f, 5020); CHECK(f.core.getDeviceState().pump_on);
    }
    {
      Fixture f; boot(f); heartbeat(f, 100, 5001);
      sample(f, 5000, "START\n");
      for (unsigned t = 5100; t <= 5500; t += 100) sample(f, t);
      CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
      CHECK(f.core.getState() == EquipmentState::Ready);
      sample(f, 5600);
      CHECK(f.core.getState() == EquipmentState::Run);
      heartbeat(f, 5700, 35501);
      sample(f, 35599);
      CHECK(f.core.getState() == EquipmentState::Run);
      CHECK(f.core.getDeviceState().pump_on && f.core.getDeviceState().heater_on);
      CHECK(sample(f, 35600, "NOISE\n").find("STATE:IDLE") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      heartbeat(f, 35700, 40600);
      CHECK(sample(f, 40599).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 40600).find("STATE:READY") == std::string::npos);
      sample(f, 40610, "DOOR:OPEN\n"); sample(f, 40620, "DOOR:CLOSED\n");
      heartbeat(f, 40700, 45600);
      CHECK(sample(f, 45619).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 45620).find("STATE:READY") != std::string::npos);
    }
    {
      Fixture f; boot(f);
      CHECK(sample(f, 10, "ESTOP:ON\n").find("BUZZER:ON") != std::string::npos);
      sample(f, 20, "RESET\n"); CHECK(f.core.getState() == EquipmentState::Error);
      heartbeat(f, 100, 2001);
      CHECK(sample(f, 2009).find("BUZZER:ON") != std::string::npos);
      const auto muted = sample(f, 2010);
      CHECK(muted.find("BUZZER:OFF") != std::string::npos);
      CHECK(muted.find("STATE:ERROR") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Error);
      sample(f, 2020, "ESTOP:OFF\nDOOR:OPEN\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      sample(f, 2030, "DOOR:CLOSED\nPRESSURE:0\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      CHECK(sample(f, 2040, "PRESSURE:2048\nRESET\n").find("STATE:IDLE") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      sample(f, 2050, "START\n"); CHECK(f.core.getState() == EquipmentState::Idle);
      heartbeat(f, 2100, 7100);
      CHECK(sample(f, 7100).find("STATE:READY") != std::string::npos);
    }
    {
      struct Capture {
        std::ostringstream out, err;
        std::streambuf* cout_old = std::cout.rdbuf(out.rdbuf());
        std::streambuf* cerr_old = std::cerr.rdbuf(err.rdbuf());
        ~Capture() { std::cout.rdbuf(cout_old); std::cerr.rdbuf(cerr_old); }
      } capture;
      Fixture f; boot(f);
      for (unsigned t = 10; t < 1000; t += 10)
      {
        CHECK(sample(f, t, "PRESSURE:" + std::to_string(t) + "\n").find("PUMP:OFF") != std::string::npos);
        CHECK(f.core.getSensorState().pressure == static_cast<int>(t));
      }
      const auto first = capture.out.str();
      CHECK(first.find("STATUS ") == 0);
      CHECK(first.find("STATUS ", 1) == std::string::npos);
      CHECK(first.find("Humidity=unavailable") != std::string::npos);
      sample(f, 1000);
      CHECK(capture.out.str().find("STATUS ", 1) != std::string::npos);
      const auto status_size = capture.out.str().size();
      const auto events_size = capture.err.str().size();
      sample(f, 1010, "ESTOP:ON\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      CHECK(capture.out.str().size() == status_size);
      CHECK(capture.err.str().size() > events_size);
    }
    {
      Fixture f; boot(f); heartbeat(f, 100, 5001);
      sample(f, 5010, "START\n");
      for (unsigned t = 5020; t <= 5070; t += 10) sample(f, t);
      CHECK(f.core.getState() == EquipmentState::Run);
      const auto response = sample(f, 5080, "DOOR:OPEN\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::DoorOpen);
      CHECK(response.find("STATE:ERROR") != std::string::npos); safe(f.core);
    }
    {
      Fixture f; boot(f); f.at(1000); f.endpoint->drain();
      f.at(1500); f.endpoint->drain();
      sample(f, 1510, "PRESSURE:2048\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      sample(f, 1520, "DOOR:CLOSED\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      sample(f, 1530, "ESTOP:OFF\nTEMP:25\nHUMIDITY:45\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Error);
      sample(f, 1531, "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25\nHUMIDITY:45\nRESET\n");
      CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(sample(f, 1540, "ESTOP:ON\n").find("BUZZER:ON") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Error);
      CHECK(f.endpoint->drain().empty());
    }
    std::cout << "Runtime policy Host PASS\n";
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
