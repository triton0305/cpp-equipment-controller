#include "runtime_fixture.hpp"
#include <sstream>

static const std::string normal = "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:1500\nTEMP:25.0\nHUMIDITY:45.0\n";
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
  sample(f, 5001, "Start\n");
  CHECK(f.core.getState() == EquipmentState::Run);
  CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
  CHECK(!f.core.getDeviceState().pump_on && !f.core.getDeviceState().heater_on);
  sample(f, 5060, normal + normal + normal);
  CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
  return 5001;
}

struct CaptureEvents
{
  std::ostringstream events;
  std::streambuf* previous = std::cerr.rdbuf(events.rdbuf());
  ~CaptureEvents() { std::cerr.rdbuf(previous); }
};

static void timeline()
{
  CaptureEvents capture;
  Fixture f;
  const auto start = startRun(f);
  unsigned last = 5060;
  const unsigned boundaries[] = {100, 3000, 5999, 6000, 6001, 11999, 12000, 12001,
    17999, 18000, 18001, 23999, 24000, 24001, 29999, 30000, 30001};
  for (const auto elapsed : boundaries)
  {
    keepAlive(f, last + 100, start + elapsed);
    // Rejected START / RESET traffic cannot defer a timed transition.
    const auto packet = sample(f, start + elapsed, normal + "START\nRESET\n");
    const auto& d = f.core.getDeviceState();
    CHECK(d.pump_on == (elapsed >= 6000 && elapsed < 24000));
    CHECK(d.heater_on == (elapsed >= 12000 && elapsed < 18000));
    CHECK(packet.find(d.pump_on ? "PUMP:ON" : "PUMP:OFF") != std::string::npos);
    CHECK(packet.find(d.heater_on ? "HEATER:ON" : "HEATER:OFF") != std::string::npos);
    CHECK(f.core.getState() == (elapsed < 30000 ? EquipmentState::Run : EquipmentState::Idle));
    CHECK(d.run_led == (elapsed < 30000));
    if (elapsed == 30000) CHECK(f.core.getSequenceStep() == SequenceStep::ProcessComplete);
    last = start + elapsed;
  }
  std::size_t event_end = 0;
  for (const auto* event : {"PROCESS START", "PROCESS PUMP ON", "PROCESS HEATER ON",
      "PROCESS HEATER OFF", "PROCESS PUMP OFF", "PROCESS COMPLETE"})
  {
    const auto pos = capture.events.str().find(event, event_end);
    CHECK(pos != std::string::npos);
    CHECK(capture.events.str().find(event, pos + 1) == std::string::npos);
    event_end = pos + std::string(event).size();
  }
  sample(f, 35010); sample(f, 35020);
  CHECK(f.core.getSequenceStep() == SequenceStep::None);
  keepAlive(f, 35100, 45000);
  CHECK(sample(f, 45010).find("STATE:READY") == std::string::npos);
  sample(f, 45020, "START\n"); CHECK(f.core.getState() == EquipmentState::Idle);
  sample(f, 45030, "DOOR:OPEN\n");
  sample(f, 45040, "DOOR:CLOSED\n"); keepAlive(f, 45100, 50000);
  CHECK(sample(f, 50039).find("STATE:READY") == std::string::npos);
  CHECK(sample(f, 50040).find("STATE:READY") != std::string::npos);
  sample(f, 50050, "START\n"); CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
  CHECK(!f.core.getDeviceState().pump_on && !f.core.getDeviceState().heater_on);
  sample(f, 50060, "STOP\n"); CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
}

static void faultsInEveryStage()
{
  const struct { const char* data; AlarmCode alarm; } faults[] = {
    {"DOOR:OPEN\n", AlarmCode::DoorOpen}, {"ESTOP:ON\n", AlarmCode::EmergencyStop},
    {"TEMP:50\nTEMP:50\n", AlarmCode::TemperatureCritical},
    {nullptr, AlarmCode::CommunicationFault}
  };
  for (unsigned stage = 0; stage < 5; ++stage)
  for (const auto& fault : faults)
  {
    Fixture f; const auto start = startRun(f);
    const auto at = start + stage * 6000 + 1500;
    keepAlive(f, 5100, at - 1000); sample(f, at - 1000);
    CHECK(f.core.getDeviceState().pump_on == (stage >= 1 && stage <= 3));
    CHECK(f.core.getDeviceState().heater_on == (stage == 2));
    std::string packet;
    if (fault.data)
    {
      keepAlive(f, at - 900, at - 100);
      packet = sample(f, at, fault.data);
    }
    else { f.at(at); packet = f.endpoint->drain(); }
    CHECK(f.core.getState() == EquipmentState::Error);
    CHECK(f.core.getAlarmCode() == fault.alarm); safe(f.core);
    CHECK(packet.find("PUMP:OFF") != std::string::npos);
    CHECK(packet.find("HEATER:OFF") != std::string::npos);
    CHECK(packet.find("RUN_LED:OFF") != std::string::npos);
  }
}

static void tickAndValidity()
{
  Fixture f; const auto start = startRun(f);
  unsigned last = 5060;
  for (unsigned elapsed = 6000; elapsed <= 30000; elapsed += 6000)
  {
    keepAlive(f, last + 100, start + elapsed - 100);
    sample(f, start + elapsed - 1);
    // No received data or command is needed to advance a deadline.
    f.at(start + elapsed);
    const auto packet = f.endpoint->drain();
    CHECK(f.core.getDeviceState().pump_on == (elapsed < 24000));
    CHECK(f.core.getDeviceState().heater_on == (elapsed == 12000));
    CHECK(!packet.empty());
    last = start + elapsed;
  }
  safe(f.core); CHECK(f.core.getState() == EquipmentState::Idle);

  Fixture missing; const auto accepted = startRun(missing);
  // Existing validity policy: individual stale/invalid readings invalidate the
  // measurement and break critical streaks; GPIO traffic keeps the link alive.
  // These flags are not new pressure/temperature progression targets.
  keepAlive(missing, 5100, accepted + 12100, "DOOR:CLOSED\nESTOP:OFF\n");
  CHECK(!missing.core.getSensorState().pressure_valid);
  CHECK(!missing.core.getSensorState().temperature_valid);
  CHECK(!missing.core.getSensorState().humidity_valid);
  CHECK(missing.core.getDeviceState().pump_on && missing.core.getDeviceState().heater_on);
  sample(missing, accepted + 12200, "TEMP:INVALID\nPRESSURE:INVALID\n");
  CHECK(missing.core.getState() == EquipmentState::Run);
  sample(missing, accepted + 12300, "TEMP:50\n");
  CHECK(missing.core.getState() == EquipmentState::Run);
  sample(missing, accepted + 12400, "TEMP:50\n");
  CHECK(missing.core.getAlarmCode() == AlarmCode::TemperatureCritical); safe(missing.core);
}

static void delayedTickAndRestart()
{
  Fixture f; const auto start = startRun(f);
  keepAlive(f, 5100, start + 5900);
  sample(f, start + 6017);
  CHECK(f.core.getDeviceState().pump_on && !f.core.getDeviceState().heater_on);
  keepAlive(f, start + 6100, start + 11900);
  sample(f, start + 12000);
  CHECK(f.core.getDeviceState().heater_on); // No accumulated 17ms drift.
  sample(f, start + 12001, "STOP\n"); safe(f.core);
  sample(f, start + 12002);
  keepAlive(f, start + 12100, start + 17000);
  sample(f, start + 17002, "START\n");
  CHECK(f.core.getSequenceStep() == SequenceStep::ProcessStart);
  CHECK(!f.core.getDeviceState().pump_on && !f.core.getDeviceState().heater_on);
  keepAlive(f, start + 17100, start + 22900);
  sample(f, start + 23001); CHECK(!f.core.getDeviceState().pump_on);
  sample(f, start + 23002); CHECK(f.core.getDeviceState().pump_on);
}

int main()
{
  try
  {
    for (auto text : {"START", "Start", "start", "sTaRt"}) CHECK(parseCommand(text).type == CommandType::Start);
    for (auto text : {"STOP", "Stop", "stop"}) CHECK(parseCommand(text).type == CommandType::Stop);
    for (auto text : {"RESET", "Reset", "reset"}) CHECK(parseCommand(text).type == CommandType::Reset);
    CHECK(parseCommand("START NOW").type == CommandType::Invalid);
    timeline();
    faultsInEveryStage();
    tickAndValidity();
    delayedTickAndRestart();
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
      const std::string other = key == "PRESSURE:" ? "DOOR:CLOSED\nESTOP:OFF\n" : "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:1500\n";
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
