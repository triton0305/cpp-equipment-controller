
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: test_runtime_uart <controller_path>\n";
    return 1;
  }

  int master = -1;
  int slave = -1;
  if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0)
  {
    perror("openpty");
    return 1;
  }

  const char* device = ttyname(slave);
  if (!device)
  {
    perror("ttyname");
    close(master);
    close(slave);
    return 1;
  }

  const pid_t pid = fork();
  if (pid < 0)
  {
    perror("fork");
    close(master);
    close(slave);
    return 1;
  }

  if (pid == 0)
  {
    close(master);
    execl(argv[1], argv[1], device, nullptr);
    _exit(127);
  }

  close(slave);
  usleep(500000);

  const std::string messages =
    "TEMP:32.5\n"
    "PRESSURE:70\n"
    "DOOR:CLOSED\n"
    "ESTOP:OFF\n"
    "MOTOR:OK\n";

  if (write(master, messages.data(), messages.size()) != static_cast<ssize_t>(messages.size()))
  {
    std::cerr << "Failed to send SENSOR data\n";
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    close(master);
    return 1;
  }

  std::string received;
  const std::string expected =
    "PUMP:OFF\n"
    "HEATER:OFF\n"
    "VALVE:CLOSED\n"
    "BUZZER:OFF\n"
    "RUN_LED:OFF\n"
    "ERROR_LED:OFF\n";
  const std::size_t expected_size = expected.size() * 5;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  while (std::chrono::steady_clock::now() < deadline)
  {
    pollfd pfd{master, POLLIN, 0};
    if (poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN))
    {
      char buffer[1024];
      const ssize_t n = read(master, buffer, sizeof(buffer));
      if (n > 0)
        received.append(buffer, static_cast<std::size_t>(n));
    }

    if (received.size() >= expected_size)
      break;
  }

  bool passed = true;
  std::size_t pos = 0;

  for (int i = 0; i < 5; ++i)
  {
    if (received.compare(pos, expected.size(), expected) != 0)
    {
      passed = false;
      break;
    }
    pos += expected.size();
  }

  passed = passed && received.size() == pos;

  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
  close(master);

  std::cout << "[TEST 27] Runtime UART TX\n";
  std::cout << "Received bytes: " << received.size() << '\n';
  std::cout << (passed ? "PASS" : "FAIL") << '\n';

  return passed ? 0 : 1;
}
