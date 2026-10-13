#ifndef SERIAL_TRANSPORT_HPP
#define SERIAL_TRANSPORT_HPP

#include <cstddef>
#include <string>
#include <vector>

enum class SerialReadEventType { Line, OversizedLineDiscarded };

struct SerialReadEvent
{
  SerialReadEventType type;
  std::string line;
};

class UartDiagnostics;

class SerialTransport
{
public:
  SerialTransport() = default;
  ~SerialTransport();

  SerialTransport(const SerialTransport&) = delete;
  SerialTransport& operator=(const SerialTransport&) = delete;

  bool openPort(const std::string& device);
  void closePort();

  bool isOpen() const;
  void setDiagnostics(UartDiagnostics* diagnostics) { diagnostics_ = diagnostics; }
  void diagnosePendingInput();

  bool writeAll(const std::string& data);
  bool readAvailable(std::string& data);
  bool readLines(std::vector<std::string>& lines);
  // Ordered framing events; discard is reported once, at the overflow byte.
  // The remainder of that line is discarded through its terminating newline.
  bool readEvents(std::vector<SerialReadEvent>& events);

private:
  UartDiagnostics* diagnostics_ = nullptr;
  int fd_ = -1;

  bool discarding_line_ = false;

  std::string rx_buffer_;
  
  static constexpr std::size_t MAX_LINE_LENGTH = 256;
};

#endif