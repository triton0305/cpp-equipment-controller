#ifndef UART_DIAGNOSTICS_HPP
#define UART_DIAGNOSTICS_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Optional foreground-only flight recorder. No formatting, allocation, or I/O
// in record(). Export only after control shutdown. Times share steady_clock's
// epoch with ControllerRuntime (the injected clock is for Host fixtures).
class UartDiagnostics
{
public:
  using Clock = std::chrono::steady_clock;
  using Now = std::function<Clock::time_point()>;
  explicit UartDiagnostics(Now now = Clock::now, std::size_t capacity = 262144);
  struct Context
  {
    const char* phase = "Disconnected";
    Clock::time_point sensor{}, sync{}, recovered{};
    bool sensor_seen = false, sync_seen = false, recovered_seen = false;
    std::uint64_t session = 0, poll = 0;
    std::size_t rx_events = 0, rx_index = 0;
  } context;

  void beginPoll();
  std::int64_t record(const char* event, std::int64_t duration_us = -1,
                      std::int64_t bytes = -1, int result = -1,
                      std::string_view detail = {});
  void recordAt(const char* event, Clock::time_point at, std::string_view detail = {});
  bool save(const std::string& path) const;

  class Span
  {
  public:
    Span(UartDiagnostics* recorder, const char* begin, const char* end,
         std::int64_t requested_bytes = -1);
    ~Span();
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
    std::int64_t bytes = -1;
    int result = -1;
  private:
    UartDiagnostics* recorder_;
    const char* end_;
    std::int64_t started_ = 0;
  };

private:
  struct Record
  {
    const char* event = nullptr;
    const char* phase = nullptr;
    std::uint64_t sequence = 0, session = 0, poll = 0;
    std::int64_t time_us = 0, elapsed_us = 0, duration_us = -1;
    std::int64_t sensor_age_us = -1, sync_age_us = -1, recovery_age_us = -1;
    std::int64_t poll_gap_us = -1, max_poll_gap_us = 0, bytes = -1;
    std::size_t rx_events = 0, rx_index = 0;
    int result = -1;
    char detail[48]{}; // Raw prefix, exported as hex; no terminal control bytes.
    std::size_t detail_size = 0;
  };
  std::int64_t append(const char* event, Clock::time_point now, std::int64_t duration_us,
                      std::int64_t bytes, int result, std::string_view detail);
  Now now_;
  std::vector<Record> records_;
  std::size_t prefix_capacity_;
  std::uint64_t total_ = 0;
  Clock::time_point previous_{}, previous_poll_{};
  bool poll_seen_ = false;
  std::int64_t poll_gap_us_ = -1, max_poll_gap_us_ = 0;
};
#endif
