#include "serial_transport.hpp"
#include "uart_diagnostics.hpp"
#include <sys/ioctl.h>

#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <chrono>

SerialTransport::~SerialTransport()
{
  closePort();
}

bool SerialTransport::openPort(const std::string& device)
{
  closePort();

  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);

  if (fd_ < 0)
  {
    return false;
  }

  termios tty{};

  if (tcgetattr(fd_, &tty) != 0)
  {
    closePort();
    return false;
  }

  cfmakeraw(&tty);

  if (cfsetispeed(&tty, B115200) != 0 ||
      cfsetospeed(&tty, B115200) != 0)
  {
    closePort();
    return false;
  }

  tty.c_cflag &= ~CSIZE;
  tty.c_cflag |= CS8;
  tty.c_cflag &= ~PARENB;
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag |= CLOCAL | CREAD;

  tty.c_cflag &= ~CRTSCTS;

  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd_, TCSANOW, &tty) != 0)
  {
    closePort();
    return false;
  }

  // openPort already clears the application line buffer through closePort.
  // Also discard input queued by the OS before this connection was established.
  if (tcflush(fd_, TCIFLUSH) != 0)
  {
    closePort();
    return false;
  }

  return true;
}

void SerialTransport::closePort()
{
  if (fd_ >= 0)
  {
    ::close(fd_);
    fd_ = -1;
  }

  rx_buffer_.clear();
  discarding_line_ = false;
}

bool SerialTransport::isOpen() const
{
  return fd_ >= 0;
}

bool SerialTransport::writeAll(const std::string& data)
{
  UartDiagnostics::Span trace(diagnostics_, "write_begin", "write_end", data.size());
  trace.result = 0;
  trace.bytes = 0;
  if (!isOpen())
  {
    return false;
  }

  std::size_t written = 0;

  const auto deadline =
    std::chrono::steady_clock::now() +
    std::chrono::milliseconds(1000);

  while (written < data.size())
  {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    const ssize_t result = ::write(
      fd_,
      data.data() + written,
      data.size() - written
    );

    if (result > 0)
    {
      written += static_cast<std::size_t>(result);
      trace.bytes = written;
      continue;
    }

    if (result < 0 && errno == EINTR)
    {
      continue;
    }

    if (result < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      const auto now = std::chrono::steady_clock::now();

      if (now >= deadline)
      {
        return false;
      }

      const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - now
        ).count();

      pollfd descriptor{};
      descriptor.fd = fd_;
      descriptor.events = POLLOUT;

      const int poll_result = ::poll(
        &descriptor, 1, static_cast<int>(remaining)
      );

      if (poll_result > 0)
      {
        if (descriptor.revents &
            (POLLERR | POLLHUP | POLLNVAL))
        {
          return false;
        }

        continue;
      }

      if (poll_result < 0 && errno == EINTR)
      {
        continue;
      }

      return false;
    }

    return false;
  }

  trace.result = 1;
  return true;
}

void SerialTransport::diagnosePendingInput()
{
  if (!diagnostics_) return;
  int bytes = -1;
  const int result = ioctl(fd_, FIONREAD, &bytes);
  diagnostics_->record("timeout_rx_queue", -1, result == 0 ? bytes : -1, result == 0);
}

bool SerialTransport::readAvailable(std::string& data)
{
  data.clear();

  if (!isOpen())
  {
    return false;
  }

  char buffer[256];

  // Bound one read pass so continuous noise cannot starve runtime deadlines.
  while (data.size() < 4096U)
  {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int poll_result;

    do
    {
      poll_result = ::poll(&pfd, 1, 0);
    }
    while (poll_result < 0 && errno == EINTR);

    if (poll_result < 0)
    {
      closePort();
      return false;
    }

    if (poll_result > 0 &&
        (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)))
    {
      closePort();
      return false;
    }

    const ssize_t result =
      ::read(fd_, buffer, sizeof(buffer));

    if (result > 0)
    {
      data.append(
        buffer,
        static_cast<std::size_t>(result)
      );

      continue;
    }

    if (result < 0 && errno == EINTR)
    {
      continue;
    }

    if (result < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      return true;
    }

    if (result == 0)
    {
      return true;
    }

    closePort();
    return false;
  }
  return true;
}

bool SerialTransport::readLines(
  std::vector<std::string>& lines
)
{
  lines.clear();

  std::vector<SerialReadEvent> events;
  if (!readEvents(events)) return false;
  for (const auto& event : events)
  {
    if (event.type == SerialReadEventType::Line) lines.push_back(event.line);
  }
  return true;
}

bool SerialTransport::readEvents(std::vector<SerialReadEvent>& events)
{
  UartDiagnostics::Span trace(diagnostics_, "read_events_begin", "read_events_end");
  trace.result = 0;
  events.clear();
  std::string received;

  if (!readAvailable(received))
  {
    return false;
  }

  trace.bytes = received.size();
  if (diagnostics_) diagnostics_->record("rx_bytes", -1, received.size(), 1, received);
  for (char ch : received)
  {
    if (discarding_line_)
    {
      if (ch == '\n')
      {
        discarding_line_ = false;
      }

      continue;
    }

    if (ch == '\n')
    {
      std::string line = rx_buffer_;

      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }

      events.push_back({SerialReadEventType::Line, line});
      rx_buffer_.clear();
      continue;
    }

    if (rx_buffer_.size() >= MAX_LINE_LENGTH)
    {
      rx_buffer_.clear();
      discarding_line_ = true;
      events.push_back({SerialReadEventType::OversizedLineDiscarded, {}});
      continue;
    }

    rx_buffer_ += ch;
  }

  if (diagnostics_) diagnostics_->context.rx_events = events.size();
  trace.result = 1;
  return true;
}
