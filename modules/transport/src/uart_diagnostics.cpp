#include "uart_diagnostics.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <utility>

namespace {
std::int64_t us(UartDiagnostics::Clock::duration value)
{ return std::chrono::duration_cast<std::chrono::microseconds>(value).count(); }
}

UartDiagnostics::UartDiagnostics(Now now, std::size_t capacity)
  : now_(std::move(now)), records_(std::max<std::size_t>(capacity, 4)),
    prefix_capacity_(records_.size() / 4)
{
}

void UartDiagnostics::beginPoll()
{
  const auto now = now_();
  if (poll_seen_) poll_gap_us_ = us(now - previous_poll_);
  max_poll_gap_us_ = std::max(max_poll_gap_us_, poll_gap_us_);
  previous_poll_ = now;
  poll_seen_ = true;
  ++context.poll;
  context.rx_events = context.rx_index = 0;
}

std::int64_t UartDiagnostics::append(const char* event, Clock::time_point now,
    std::int64_t duration_us, std::int64_t bytes, int result, std::string_view detail)
{
  const auto index = total_ < prefix_capacity_ ? total_
      : prefix_capacity_ + (total_ - prefix_capacity_) % (records_.size() - prefix_capacity_);
  auto& r = records_[index];
  r = Record{};
  r.event = event; r.phase = context.phase;
  r.sequence = total_++; r.session = context.session; r.poll = context.poll;
  r.time_us = us(now.time_since_epoch());
  r.elapsed_us = r.sequence ? us(now - previous_) : 0;
  previous_ = now;
  r.duration_us = duration_us;
  if (context.sensor_seen) r.sensor_age_us = us(now - context.sensor);
  if (context.sync_seen) r.sync_age_us = us(now - context.sync);
  if (context.recovered_seen) r.recovery_age_us = us(now - context.recovered);
  r.poll_gap_us = poll_gap_us_; r.max_poll_gap_us = max_poll_gap_us_;
  r.rx_events = context.rx_events; r.rx_index = context.rx_index;
  r.bytes = bytes; r.result = result;
  r.detail_size = std::min(detail.size(), sizeof(r.detail));
  std::copy_n(detail.data(), r.detail_size, r.detail);
  return r.time_us;
}

std::int64_t UartDiagnostics::record(const char* event, std::int64_t duration_us,
    std::int64_t bytes, int result, std::string_view detail)
{
  return append(event, now_(), duration_us, bytes, result, detail);
}

void UartDiagnostics::recordAt(const char* event, Clock::time_point at, std::string_view detail)
{
  append(event, at, -1, -1, -1, detail);
}

UartDiagnostics::Span::Span(UartDiagnostics* recorder, const char* begin,
    const char* end, std::int64_t requested_bytes) : recorder_(recorder), end_(end)
{
  if (recorder_) started_ = recorder_->record(begin, -1, requested_bytes);
}
UartDiagnostics::Span::~Span()
{
  if (!recorder_) return;
  const auto now = recorder_->now_();
  recorder_->append(end_, now, us(now.time_since_epoch()) - started_, bytes, result, {});
}

bool UartDiagnostics::save(const std::string& path) const
{
  std::ofstream out(path);
  if (!out) return false;
  out.imbue(std::locale::classic());
  out << "# UART_DIAG version=1 total=" << total_ << " capacity=" << records_.size()
      << " dropped=" << (total_ > records_.size() ? total_ - records_.size() : 0)
      << " record_bytes=" << sizeof(Record) << '\n';
  out << "sequence,monotonic_us,elapsed_ms,event,phase,session,poll,last_sensor_age_ms,"
         "sync_age_ms,recovery_age_ms,rx_events,rx_index,bytes,result,duration_ms,"
         "poll_gap_ms,max_poll_gap_ms,detail_hex\n";
  out << std::fixed << std::setprecision(3);
  const auto emit = [&](const Record& r) {
    const auto ms = [](std::int64_t value) { return value < 0 ? -1.0 : value / 1000.0; };
    out << r.sequence << ',' << r.time_us << ',' << ms(r.elapsed_us) << ',' << r.event
        << ',' << r.phase << ',' << r.session << ',' << r.poll << ',' << ms(r.sensor_age_us)
        << ',' << ms(r.sync_age_us) << ',' << ms(r.recovery_age_us) << ',' << r.rx_events
        << ',' << r.rx_index << ',' << r.bytes << ',' << r.result << ',' << ms(r.duration_us)
        << ',' << ms(r.poll_gap_us) << ',' << ms(r.max_poll_gap_us) << ',';
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < r.detail_size; ++i) {
      const auto byte = static_cast<unsigned char>(r.detail[i]);
      out << hex[byte >> 4] << hex[byte & 15];
    }
    out << '\n';
  };
  for (std::uint64_t i = 0; i < std::min<std::uint64_t>(total_, prefix_capacity_); ++i)
    emit(records_[i]);
  const auto tail_capacity = records_.size() - prefix_capacity_;
  const auto first = std::max<std::uint64_t>(prefix_capacity_, total_ > tail_capacity ? total_ - tail_capacity : 0);
  for (auto i = first; i < total_; ++i)
    emit(records_[prefix_capacity_ + (i - prefix_capacity_) % tail_capacity]);
  out.flush();
  return static_cast<bool>(out);
}
