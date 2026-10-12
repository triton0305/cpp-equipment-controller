#include "runtime_fixture.hpp"
#include <sstream>

static const std::string normal = "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\nTEMP:25.0\nHUMIDITY:45.0\n";
static std::string sample(Fixture& f, unsigned ms, const std::string& data = normal)
{
  f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(ms);
  f.receive(data); return f.endpoint->drain();
}
static void keepAlive(Fixture& f, unsigned first, unsigned last, const std::string& data = normal)
{
  for (unsigned t = first; t <= last; t += 100) sample(f, t, data);
}
static void qualify(Fixture& f)
{
  f.runtime->poll(); CHECK(f.endpoint->drain().find("STATE:SAFE") != std::string::npos);
  CHECK(sample(f, 0).find("STATE:IDLE") != std::string::npos);
  keepAlive(f, 100, 4900);
  CHECK(sample(f, 4999).find("STATE:READY") == std::string::npos);
  sample(f, 4999, "start\n"); CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
  CHECK(sample(f, 5000).find("STATE:READY") != std::string::npos); safe(f.core);
}
static unsigned startRun(Fixture& f)
{
  qualify(f);
  sample(f, 5001, "Start\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckDoor); safe(f.core);
  const SequenceStep steps[] = {SequenceStep::PumpOn, SequenceStep::CheckPressure,
    SequenceStep::HeaterOn, SequenceStep::CheckTemperature, SequenceStep::ProcessStart, SequenceStep::ProcessRunning};
  for (unsigned i = 0; i < 6; ++i)
  {
    // Many valid sensor lines still perform exactly one sequence transition.
    const auto packet = sample(f, 5010 + 10 * i, normal + normal + normal);
    CHECK(f.core.getSequenceStep() == steps[i]);
    CHECK(f.core.getDeviceState().pump_on);
    CHECK(f.core.getDeviceState().heater_on == (i >= 2));
    if (i < 5) CHECK(f.core.getState() == EquipmentState::Ready);
    else CHECK(packet.find("STATE:RUN") != std::string::npos);
  }
  CHECK(f.core.getState() == EquipmentState::Run);
  return 5060;
}

int main()
{
  try
  {
    for (auto text : {"START", "Start", "start", "sTaRt"}) CHECK(parseCommand(text).type == CommandType::Start);
    for (auto text : {"STOP", "Stop", "stop"}) CHECK(parseCommand(text).type == CommandType::Stop);
    for (auto text : {"RESET", "Reset", "reset"}) CHECK(parseCommand(text).type == CommandType::Reset);
    CHECK(parseCommand("START NOW").type == CommandType::Invalid);
    {
      Fixture f; qualify(f);
      sample(f, 5001, "start\n" + normal + normal);
      CHECK(f.core.getSequenceStep() == SequenceStep::CheckDoor); safe(f.core);
      sample(f, 5010); CHECK(f.core.getSequenceStep() == SequenceStep::PumpOn);
      CHECK(f.core.getDeviceState().pump_on && !f.core.getDeviceState().heater_on);
      sample(f, 5020, "PRESSURE:200\n");
      CHECK(f.core.getSequenceStep() == SequenceStep::CheckPressure);
      sample(f, 5030, "DOOR:CLOSED\n");
      CHECK(f.core.getSequenceStep() == SequenceStep::CheckPressure);
      CHECK(!f.core.getDeviceState().heater_on);
      sample(f, 5040, "PRESSURE:INVALID\n");
      // No pressure or temperature measurement for >5s; safety GPIOs stay fresh.
      keepAlive(f, 5100, 10400, "DOOR:CLOSED\nESTOP:OFF\n");
      CHECK(!f.core.getSensorState().pressure_valid && !f.core.getSensorState().temperature_valid);
      CHECK(f.core.getSequenceStep() == SequenceStep::CheckPressure);
      sample(f, 10500, "PRESSURE:2048\n");
      CHECK(f.core.getSequenceStep() == SequenceStep::HeaterOn && f.core.getDeviceState().heater_on);
      sample(f, 10510, "DOOR:CLOSED\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckTemperature);
      sample(f, 10520, "TEMP:INVALID\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckTemperature);
      sample(f, 10530, "TEMP:50.0\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckTemperature);
      CHECK(f.core.getState() != EquipmentState::Error);
      sample(f, 10540, "TEMP:25.0\n"); CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
      sample(f, 10550, "TEMP:INVALID\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckTemperature);
      sample(f, 10560); CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
      sample(f, 10570); CHECK(f.core.getState() == EquipmentState::Run);
      keepAlive(f, 10670, 40470);
      sample(f, 40569); CHECK(f.core.getState() == EquipmentState::Run);
      const auto ended = sample(f, 40570, "start\n"); // Ignored command cannot delay RUN timeout.
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      CHECK(ended.find("STATE:IDLE") != std::string::npos);
      CHECK(f.core.getSequenceStep() == SequenceStep::ProcessComplete);
      sample(f, 40580); CHECK(f.core.getSequenceStep() == SequenceStep::Shutdown);
      sample(f, 40590); CHECK(f.core.getSequenceStep() == SequenceStep::None);
      keepAlive(f, 40600, 50600);
      CHECK(sample(f, 50610).find("STATE:READY") == std::string::npos);
      sample(f, 50620, "START\n"); CHECK(f.core.getState() == EquipmentState::Idle);
      CHECK(sample(f, 50630, "DOOR:OPEN\n").find("STATE:IDLE") != std::string::npos);
      sample(f, 50640, "DOOR:CLOSED\n"); keepAlive(f, 50700, 55600);
      CHECK(sample(f, 55639).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 55640).find("STATE:READY") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      sample(f, 55650, "start\n"); CHECK(f.core.getSequenceStep() == SequenceStep::CheckDoor);
      sample(f, 55660, "Stop\n"); CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
    }
    {
      Fixture f; startRun(f);
      sample(f, 5070, "TEMP:5.1\nTEMP:5.1\nHUMIDITY:15.1\nHUMIDITY:15.1\nPRESSURE:201\nPRESSURE:201\n");
      CHECK(f.core.getState() == EquipmentState::Run);
      sample(f, 5080, "TEMP:49.9\nTEMP:49.9\nHUMIDITY:89.9\nHUMIDITY:89.9\nPRESSURE:3899\nPRESSURE:3899\n");
      CHECK(f.core.getState() == EquipmentState::Run);
    }
    const struct { const char* reading; AlarmCode alarm; } critical[] = {
      {"TEMP:5.0\n", AlarmCode::TemperatureCritical}, {"TEMP:50.0\n", AlarmCode::TemperatureCritical},
      {"HUMIDITY:15.0\n", AlarmCode::HumidityCritical}, {"HUMIDITY:90.0\n", AlarmCode::HumidityCritical},
      {"PRESSURE:200\n", AlarmCode::PressureCritical}, {"PRESSURE:3900\n", AlarmCode::PressureCritical}
    };
    for (const auto& c : critical)
    {
      Fixture f; startRun(f);
      sample(f, 5070, c.reading); CHECK(f.core.getState() == EquipmentState::Run);
      // Poll count, identical cached values, and unrelated sensor traffic are not new samples.
      for (unsigned ms = 5080; ms <= 5200; ms += 10) f.at(ms);
      f.endpoint->drain(); sample(f, 5210, "DOOR:CLOSED\nESTOP:OFF\n");
      CHECK(f.core.getState() == EquipmentState::Run);
      const auto error = sample(f, 5300, c.reading);
      CHECK(f.core.getState() == EquipmentState::Error && f.core.getAlarmCode() == c.alarm);
      CHECK(error.find("STATE:ERROR") != std::string::npos && error.find("BUZZER:ON") != std::string::npos);
      safe(f.core);
      sample(f, 5310, "Reset\n"); CHECK(f.core.getState() == EquipmentState::Error);
      keepAlive(f, 5400, 7200); CHECK(f.core.getState() == EquipmentState::Error);
      CHECK(sample(f, 7299).find("BUZZER:ON") != std::string::npos);
      const auto muted = sample(f, 7300);
      CHECK(muted.find("BUZZER:OFF") != std::string::npos && muted.find("STATE:ERROR") != std::string::npos);
      CHECK(f.core.getAlarmCode() == c.alarm);
      CHECK(sample(f, 7310, "reset\n").find("STATE:IDLE") != std::string::npos);
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      keepAlive(f, 7400, 12200);
      CHECK(sample(f, 12309).find("STATE:READY") == std::string::npos);
      CHECK(sample(f, 12310).find("STATE:READY") != std::string::npos); // Error reset needs no Door re-arm.
    }
    for (const auto& key : {std::string("TEMP:"), std::string("HUMIDITY:"), std::string("PRESSURE:")})
    {
      const auto low = key == "TEMP:" ? "5" : key == "HUMIDITY:" ? "15" : "200";
      const auto high = key == "TEMP:" ? "50" : key == "HUMIDITY:" ? "90" : "3900";
      const auto good = key == "TEMP:" ? "25" : key == "HUMIDITY:" ? "45" : "2048";
      Fixture f; startRun(f);
      sample(f, 5070, key + low + "\n");
      sample(f, 5080, key + "INVALID\n");
      sample(f, 5090, key + low + "\n"); CHECK(f.core.getState() == EquipmentState::Run);
      sample(f, 5100, key + good + "\n");
      sample(f, 5110, key + low + "\n"); CHECK(f.core.getState() == EquipmentState::Run);
      sample(f, 5120, key + high + "\n"); CHECK(f.core.getState() == EquipmentState::Run); // Different critical side.
      sample(f, 5130, key + high + "\n"); CHECK(f.core.getState() == EquipmentState::Error);
    }
    for (const auto& key : {std::string("TEMP:"), std::string("HUMIDITY:"), std::string("PRESSURE:")})
    {
      Fixture f; startRun(f);
      const auto critical = key == "TEMP:" ? "50" : key == "HUMIDITY:" ? "90" : "3900";
      const unsigned expiry = key == "PRESSURE:" ? 1000 : 5000;
      const std::string other = key == "PRESSURE:" ? "DOOR:CLOSED\nESTOP:OFF\n" : "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:2048\n";
      sample(f, 5070, key + critical + "\n");
      keepAlive(f, 5170, 5070 + expiry, other);
      sample(f, 5080 + expiry, key + critical + "\n");
      CHECK(f.core.getState() == EquipmentState::Run); // Staleness broke the previous streak.
      sample(f, 5090 + expiry, key + critical + "\n");
      CHECK(f.core.getState() == EquipmentState::Error);
    }
    {
      Fixture f; qualify(f); sample(f, 5010, "start\n");
      // Critical safety also covers preparation, before RUN entry.
      sample(f, 5020, "HUMIDITY:90\nHUMIDITY:90\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::HumidityCritical); safe(f.core);
      sample(f, 5030, normal + "TEMP:INVALID\nreset\n");
      CHECK(f.core.getState() == EquipmentState::Error); // Missing actual safe measurement cannot reset.
      sample(f, 5040, "TEMP:25\nreset\n"); CHECK(f.core.getState() == EquipmentState::Idle);
    }
    {
      Fixture f; startRun(f);
      sample(f, 5070, "TEMP:50\nESTOP:ON\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop); safe(f.core);
      sample(f, 5080, "TEMP:50\n"); CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
      f.at(6080); f.endpoint->drain(); CHECK(!f.core.getSensorState().communication_ok);
      CHECK(f.core.getAlarmCode() == AlarmCode::EmergencyStop);
    }
    {
      Fixture f; startRun(f); sample(f, 5070, "DOOR:OPEN\n");
      CHECK(f.core.getAlarmCode() == AlarmCode::DoorOpen); safe(f.core);
    }
    std::cout << "Cycle sequencing, re-arm and critical sensor safety Host PASS\n";
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
