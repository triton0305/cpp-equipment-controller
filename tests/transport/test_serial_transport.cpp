#include <iostream>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <vector>

#include "serial_transport.hpp"


int main()
{
  std::cout << "\n[TEST 20] Serial transport basic\n";

  SerialTransport serial;

  std::cout
    << "Initial open=" << serial.isOpen()
    << '\n';

  const bool opened =
    serial.openPort("/dev/this_device_does_not_exist");

  std::cout
    << "Invalid port opened=" << opened
    << " | isOpen=" << serial.isOpen()
    << '\n';

  serial.closePort();

  std::cout
    << "After close isOpen=" << serial.isOpen()
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 21] Serial transport closed port\n";

  SerialTransport closed_serial;
  std::string received;

  const bool write_result =
    closed_serial.writeAll("PUMP:ON\n");

  const bool read_result =
    closed_serial.readAvailable(received);

  std::cout
    << "Write=" << write_result
    << " | Read=" << read_result
    << " | Received bytes=" << received.size()
    << '\n';

//=======================================================//

  std::cout << "\n[TEST 22] Virtual UART TX/RX\n";

  const int master_fd = posix_openpt(O_RDWR | O_NOCTTY);

  if (master_fd < 0)
  {
    std::cout << "PTY master open failed\n";
  }
  else if (grantpt(master_fd) != 0 ||
           unlockpt(master_fd) != 0)
  {
    std::cout << "PTY setup failed\n";
    close(master_fd);
  }
  else
  {
    char* slave_name = ptsname(master_fd);

    SerialTransport virtual_serial;

    const bool opened =
      slave_name != nullptr &&
      virtual_serial.openPort(slave_name);

    std::cout << "Virtual port opened=" << opened << '\n';

    if (opened)
    {
      // Linux → STM32 방향 (TX)
      const bool sent =
        virtual_serial.writeAll("PUMP:ON\n");

      char tx_buffer[128]{};
      const ssize_t tx_bytes =
        read(master_fd, tx_buffer, sizeof(tx_buffer));

      std::cout
        << "TX sent=" << sent
        << " | bytes=" << tx_bytes
        << " | data="
        << std::string(
             tx_buffer,
             tx_bytes > 0 ? static_cast<std::size_t>(tx_bytes) : 0
           );

      // STM32 → Linux 방향 (RX)
      const std::string response = "TEMP:28.5\n";

      const ssize_t injected =
        write(master_fd, response.data(), response.size());

      std::string received;
      const bool received_ok =
        virtual_serial.readAvailable(received);

      std::cout
        << "RX injected=" << injected
        << " | received=" << received_ok
        << " | data=" << received;

      virtual_serial.closePort();
    }

    close(master_fd);
  }

//=======================================================//

  std::cout << "\n[TEST 23] UART fragmented line receive\n";

  const int line_master = posix_openpt(O_RDWR | O_NOCTTY);

  if (line_master >= 0 &&
      grantpt(line_master) == 0 &&
      unlockpt(line_master) == 0)
  {
    char* line_slave = ptsname(line_master);

    SerialTransport line_serial;

    if (line_slave != nullptr &&
        line_serial.openPort(line_slave))
    {
      std::vector<std::string> lines;

      const std::string part1 = "TEMP:2";
      write(line_master, part1.data(), part1.size());

      line_serial.readLines(lines);

      std::cout
        << "Part 1 | complete lines=" << lines.size()
        << '\n';

      const std::string part2 = "8.5\nPRESS";
      write(line_master, part2.data(), part2.size());

      line_serial.readLines(lines);

      std::cout
        << "Part 2 | complete lines=" << lines.size();

      for (const auto& line : lines)
      {
        std::cout << " | line=" << line;
      }

      std::cout << '\n';

      const std::string part3 = "URE:75\n";
      write(line_master, part3.data(), part3.size());

      line_serial.readLines(lines);

      std::cout
        << "Part 3 | complete lines=" << lines.size();

      for (const auto& line : lines)
      {
        std::cout << " | line=" << line;
      }

      std::cout << '\n';

      line_serial.closePort();
    }

    close(line_master);
  }
  else if (line_master >= 0)
  {
    close(line_master);
  }

//=======================================================//

  std::cout << "\n[TEST 24] UART oversized line protection\n";

  const int limit_master = posix_openpt(O_RDWR | O_NOCTTY);

  if (limit_master >= 0 &&
      grantpt(limit_master) == 0 &&
      unlockpt(limit_master) == 0)
  {
    char* limit_slave = ptsname(limit_master);

    SerialTransport limit_serial;

    if (limit_slave != nullptr &&
        limit_serial.openPort(limit_slave))
    {
      std::vector<std::string> lines;

      // 256바이트를 초과하는 비정상 Line
      const std::string oversized(300, 'A');

      write(limit_master, oversized.data(), oversized.size());

      limit_serial.readLines(lines);

      std::cout
        << "Oversized input | complete lines=" << lines.size()
        << '\n';

      // 비정상 Line 종료 후 정상 메시지 수신
      const std::string recovery = "\nTEMP:30.5\n";

      write(limit_master, recovery.data(), recovery.size());

      limit_serial.readLines(lines);

      std::cout
        << "Recovery | complete lines=" << lines.size();

      for (const auto& line : lines)
      {
        std::cout << " | line=" << line;
      }

      std::cout << '\n';

      limit_serial.closePort();
    }

    close(limit_master);
  }
  else if (limit_master >= 0)
  {
    close(limit_master);
  }

//=======================================================//

  std::cout << "\n[TEST 25] UART peer disconnect\n";

  const int disconnect_master =
    posix_openpt(O_RDWR | O_NOCTTY);

  if (disconnect_master >= 0 &&
      grantpt(disconnect_master) == 0 &&
      unlockpt(disconnect_master) == 0)
  {
    char* disconnect_slave = ptsname(disconnect_master);

    SerialTransport disconnect_serial;

    if (disconnect_slave != nullptr &&
        disconnect_serial.openPort(disconnect_slave))
    {
      std::cout
        << "Before disconnect | isOpen="
        << disconnect_serial.isOpen()
        << '\n';

      // 상대 PTY Master 연결 종료
      close(disconnect_master);

      std::string received;

      const bool read_ok =
        disconnect_serial.readAvailable(received);

      std::cout
        << "After disconnect"
        << " | read_ok=" << read_ok
        << " | isOpen=" << disconnect_serial.isOpen()
        << " | received_bytes=" << received.size()
        << '\n';

      disconnect_serial.closePort();
    }
    else
    {
      close(disconnect_master);
    }
  }
  else if (disconnect_master >= 0)
  {
    close(disconnect_master);
  }

  //=======================================================//

  std::cout << "\n[TEST 26] UART reconnect after disconnect\n";

  SerialTransport reconnect_serial;

  auto create_pty = [](int& master, std::string& slave) -> bool
  {
    master = posix_openpt(O_RDWR | O_NOCTTY);

    if (master < 0)
    {
      return false;
    }

    if (grantpt(master) != 0 || unlockpt(master) != 0)
    {
      close(master);
      master = -1;
      return false;
    }

    char* name = ptsname(master);

    if (name == nullptr)
    {
      close(master);
      master = -1;
      return false;
    }

    slave = name;
    return true;
  };

  int first_master = -1;
  std::string first_slave;

  if (create_pty(first_master, first_slave) &&
      reconnect_serial.openPort(first_slave))
  {
    std::cout
      << "First connection | isOpen="
      << reconnect_serial.isOpen()
      << '\n';

    close(first_master);

    std::string received;
    const bool read_ok =
      reconnect_serial.readAvailable(received);

    std::cout
      << "After disconnect | read_ok=" << read_ok
      << " | isOpen=" << reconnect_serial.isOpen()
      << '\n';

    int second_master = -1;
    std::string second_slave;

    if (create_pty(second_master, second_slave))
    {
      const bool reopened =
        reconnect_serial.openPort(second_slave);

      std::cout
        << "Reconnect | opened=" << reopened
        << " | isOpen=" << reconnect_serial.isOpen()
        << '\n';

      if (reopened)
      {
        const std::string message = "TEMP:31.5\n";

        const ssize_t written = write(
          second_master,
          message.data(),
          message.size()
        );

        std::vector<std::string> lines;

        const bool rx_ok =
          reconnect_serial.readLines(lines);

        std::cout
          << "After reconnect"
          << " | injected=" << written
          << " | rx_ok=" << rx_ok
          << " | complete lines=" << lines.size();

        if (!lines.empty())
        {
          std::cout << " | line=" << lines.front();
        }

        std::cout << '\n';
      }

      reconnect_serial.closePort();
      close(second_master);
    }
    else
    {
      std::cout << "Second PTY creation failed\n";
    }
  }
  else
  {
    std::cout << "First PTY connection failed\n";

    if (first_master >= 0)
    {
      close(first_master);
    }
  }

  return 0;
}
