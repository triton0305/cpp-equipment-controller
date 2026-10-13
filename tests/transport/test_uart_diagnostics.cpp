#include "runtime_fixture.hpp"
#include "uart_diagnostics.hpp"
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

using Row = std::map<std::string, std::string>;
static std::vector<std::string> split(const std::string& text)
{
  std::vector<std::string> fields;
  std::istringstream in(text); std::string field;
  while (std::getline(in, field, ',')) fields.push_back(field);
  if (!text.empty() && text.back() == ',') fields.emplace_back();
  return fields;
}
static std::vector<Row> readTrace(const std::string& path, std::string& metadata)
{
  std::ifstream in(path); CHECK(in.good());
  std::getline(in, metadata);
  std::string line; std::getline(in, line); const auto columns = split(line);
  std::vector<Row> rows;
  while (std::getline(in, line))
  {
    const auto fields = split(line); CHECK(fields.size() == columns.size());
    Row row;
    for (std::size_t i = 0; i < fields.size(); ++i) row[columns[i]] = fields[i];
    rows.push_back(std::move(row));
  }
  CHECK(!in.bad()); return rows;
}
static const Row& find(const std::vector<Row>& rows, const std::string& event)
{
  for (const auto& row : rows) if (row.at("event") == event) return row;
  throw std::runtime_error("missing diagnostic event: " + event);
}
static void paired(const std::vector<Row>& rows, const std::string& begin, const std::string& end)
{
  unsigned starts = 0, ends = 0;
  for (const auto& row : rows)
  {
    if (row.at("event") == begin) ++starts;
    if (row.at("event") == end) ++ends;
  }
  CHECK(starts > 0 && starts == ends);
}
int main()
{
  try
  {
    // Trace successful synchronization and an actual telemetry gap. Recording
    // must neither rescue health nor change the existing deadline/latch policy.
    std::unique_ptr<UartDiagnostics> diag;
    Fixture f;
    diag = std::make_unique<UartDiagnostics>([&] { return f.now; }, 2048);
    f.runtime->setDiagnostics(diag.get());
    f.at(0); f.endpoint->drain();
    f.now += std::chrono::milliseconds(100);
    f.receive("DOOR:CLOSED\nESTOP:OFF\nDODOOR:OPEN\n"); f.endpoint->drain();
    CHECK(f.core.getState() == EquipmentState::Idle && f.core.getSensorState().door_closed);
    f.at(1099); f.endpoint->drain(); CHECK(f.core.getSensorState().communication_ok);
    f.at(1100); f.endpoint->drain(); fault(f.core);
    f.at(1600); f.endpoint->drain();
    f.now += std::chrono::milliseconds(10);
    f.receive("DOOR:CLOSED\nESTOP:OFF\n"); f.endpoint->drain();
    CHECK(f.core.getSensorState().communication_ok && f.core.getAlarmCode() == AlarmCode::CommunicationFault);
    f.runtime->shutdown();
    const auto path = f.directory + "/trace.csv";
    CHECK(diag->save(path)); std::string metadata;
    const auto rows = readTrace(path, metadata); CHECK(metadata.find("dropped=0") != std::string::npos);
    CHECK(find(rows, "poll_begin").at("last_sensor_age_ms") == "-1.000");
    CHECK(find(rows, "sync_complete").at("phase") == "Operational");
    CHECK(find(rows, "sync_complete").at("monotonic_us") == "100000");
    CHECK(find(rows, "last_sensor_updated").at("last_sensor_age_ms") == "0.000");
    CHECK(find(rows, "invalid_sensor").at("detail_hex") == "444f444f4f523a4f50454e");
    const auto& fault_row = find(rows, "timeout_fault");
    CHECK(fault_row.at("last_sensor_age_ms") == "1000.000");
    CHECK(fault_row.at("recovery_age_ms") == "1000.000");
    CHECK(fault_row.at("sync_age_ms") == "1100.000");
    CHECK(fault_row.at("max_poll_gap_ms") == "999.000");
    CHECK(find(rows, "timeout_rx_queue").at("result") == "1");
    CHECK(find(rows, "shutdown_complete").at("session") == "2");
    for (const auto& name : {std::string("poll"), std::string("service"), std::string("read_events"),
                            std::string("send_outputs"), std::string("write")})
      paired(rows, name + "_begin", name + "_end");
    unlink(path.c_str());

    // Successful but delayed output is distinguishable from input silence.
    std::unique_ptr<UartDiagnostics> delayed;
    Fixture slow;
    delayed = std::make_unique<UartDiagnostics>([&] { return slow.now; }, 512);
    slow.runtime->setDiagnostics(delayed.get()); slow.at(0); slow.endpoint->drain();
    slow.now += std::chrono::milliseconds(100);
    slow.endpoint->send("DOOR:CLOSED\nESTOP:OFF\nPRESSURE:1500\n");
    delayed_write_clock = &slow.now;
    slow.runtime->poll(); slow.endpoint->drain(); fault(slow.core);
    CHECK(!slow.core.getSensorState().pressure_valid);
    slow.runtime->shutdown();
    const auto slow_path = slow.directory + "/slow.csv";
    CHECK(delayed->save(slow_path)); const auto delayed_rows = readTrace(slow_path, metadata);
    bool saw_delay = false;
    for (const auto& row : delayed_rows)
      if (row.at("event") == "write_end" && row.at("duration_ms") == "1000.000")
      { CHECK(row.at("result") == "1"); saw_delay = true; }
    CHECK(saw_delay);
    CHECK(find(delayed_rows, "timeout_fault").at("rx_events") == "3");
    CHECK(find(delayed_rows, "timeout_fault").at("rx_index") == "3");
    CHECK(find(delayed_rows, "timeout_fault").at("recovery_age_ms") == "1000.000");
    CHECK(find(delayed_rows, "timeout_fault").at("max_poll_gap_ms") == "100.000");
    unlink(slow_path.c_str());

    // Bounded storage keeps the startup prefix and newest suffix in time order.
    UartDiagnostics::Clock::time_point now{};
    UartDiagnostics small([&] { return now; }, 8);
    for (int i = 0; i < 20; ++i) { small.record("test"); now += std::chrono::milliseconds(1); }
    CHECK(small.save(path)); const auto wrapped = readTrace(path, metadata);
    CHECK(metadata.find("dropped=12") != std::string::npos && wrapped.size() == 8);
    CHECK(wrapped[0].at("sequence") == "0" && wrapped[1].at("sequence") == "1");
    CHECK(wrapped[2].at("sequence") == "14" && wrapped.back().at("sequence") == "19");
    CHECK(wrapped.back().at("elapsed_ms") == "1.000");
    unlink(path.c_str());
    CHECK(!small.save(f.directory + "/missing/trace.csv"));
    std::cout << "UART diagnostics: timing, timeout, reconnect, pairing and bounded export PASS\n";
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
