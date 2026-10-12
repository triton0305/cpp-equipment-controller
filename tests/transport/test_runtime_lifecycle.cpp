#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <memory>
#include <poll.h>
#include <pty.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace
{
  void require(bool condition, const std::string& message)
  {
    if (!condition)
    {
      throw std::runtime_error(message);
    }
  }

  struct Session
  {
    int master = -1;
    int slave = -1;
    pid_t child = -1;

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ~Session()
    {
      // Failures/timeouts force termination; success requires graceful SIGTERM exit.
      if (child > 0)
      {
        kill(child, SIGKILL);
        while (waitpid(child, nullptr, 0) < 0 && errno == EINTR)
        {
        }
      }

      if (slave >= 0)
      {
        close(slave);
      }
      if (master >= 0)
      {
        close(master);
      }
    }
  };

  using Capture = std::unique_ptr<FILE, decltype(&std::fclose)>;

  std::string readCapture(FILE* file)
  {
    require(std::fseek(file, 0, SEEK_SET) == 0, "Cannot rewind capture");
    std::string text;
    char buffer[4096];
    std::size_t size;
    while ((size = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      text.append(buffer, size);
    }
    require(!std::ferror(file), "Cannot read capture");
    return text;
  }
}

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: test_runtime_lifecycle <controller_path>\n";
    return 1;
  }

  try
  {
    Capture output(std::tmpfile(), &std::fclose);
    Capture errors(std::tmpfile(), &std::fclose);
    require(output && errors, "Cannot create output captures");

    Session session;
    require(openpty(&session.master, &session.slave, nullptr, nullptr, nullptr) == 0,
            "Cannot create PTY");

    termios settings{};
    require(tcgetattr(session.slave, &settings) == 0, "Cannot read PTY settings");
    cfmakeraw(&settings);
    require(tcsetattr(session.slave, TCSANOW, &settings) == 0,
            "Cannot configure raw PTY");

    const char* name = ttyname(session.slave);
    require(name != nullptr, "Cannot identify PTY slave");
    const std::string device(name);

    session.child = fork();
    require(session.child >= 0, "fork failed");
    if (session.child == 0)
    {
      close(session.master);
      close(session.slave);
      if (dup2(fileno(output.get()), STDOUT_FILENO) < 0 ||
          dup2(fileno(errors.get()), STDERR_FILENO) < 0)
      {
        _exit(127);
      }
      close(fileno(output.get()));
      close(fileno(errors.get()));
      execl(argv[1], argv[1], device.c_str(), nullptr);
      _exit(127);
    }

    const std::array<std::string, 5> messages =
    {
      "TEMP:32.5", "PRESSURE:70", "DOOR:CLOSED", "ESTOP:OFF", "MOTOR:OK"
    };

    std::string input;
    for (const auto& message : messages)
    {
      input += message + '\n';
    }

    // Wait for the opening safe refresh: openPort intentionally flushes old RX.
    pollfd initial{session.master, POLLIN, 0};
    require(poll(&initial, 1, 2000) > 0 && (initial.revents & POLLIN),
            "Runtime did not send opening safe refresh");
    char opening[512];
    require(read(session.master, opening, sizeof(opening)) > 0, "Cannot drain opening refresh");
    std::size_t sent = 0;
    while (sent < input.size())
    {
      const ssize_t size = write(
        session.master, input.data() + sent, input.size() - sent
      );
      if (size < 0 && errno == EINTR)
      {
        continue;
      }
      require(size > 0, "Cannot send sensor input");
      sent += static_cast<std::size_t>(size);
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(10200);
    bool terminationSent = false;
    auto exitDeadline = deadline + std::chrono::seconds(3);
    int status = 0;
    while (true)
    {
      const pid_t result = waitpid(session.child, &status, WNOHANG);
      if (result == session.child)
      {
        session.child = -1;
        require(terminationSent, "Runtime exited before explicit termination");
        break;
      }
      if (result < 0 && errno == EINTR)
      {
        continue;
      }
      require(result >= 0, "waitpid failed");
      const auto now = std::chrono::steady_clock::now();
      if (!terminationSent && now >= deadline)
      {
        require(kill(session.child, SIGTERM) == 0, "Cannot terminate runtime");
        terminationSent = true;
      }
      require(now < exitDeadline, "Runtime did not exit gracefully after SIGTERM");

      pollfd descriptor{session.master, POLLIN, 0};
      const int ready = poll(&descriptor, 1, 50);
      if (ready < 0 && errno == EINTR)
      {
        continue;
      }
      require(ready >= 0, "PTY poll failed");
      require(!(descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)),
              "Unexpected PTY failure");
      if (descriptor.revents & POLLIN)
      {
        // Drain TX to avoid backpressure. TEST 27 owns the UART payload checks.
        char discarded[4096];
        const ssize_t size = read(session.master, discarded, sizeof(discarded));
        require(size >= 0 || errno == EINTR, "Cannot drain UART output");
      }
    }

    const std::string stdoutText = readCapture(output.get());
    const std::string stderrText = readCapture(errors.get());
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "Runtime did not exit with code 0\nSTDERR:\n" + stderrText);
    require(stderrText.find("Sensor stale") != std::string::npos,
            "Silent sensor stream did not fault\n" + stderrText);
    require(stderrText.find("UART read failed") == std::string::npos &&
            stderrText.find("UART write failed") == std::string::npos,
            "Unexpected UART failure\n" + stderrText);

    std::vector<std::string> accepted;
    int stateLines = 0;
    std::istringstream stream(stdoutText);
    std::string line;
    while (std::getline(stream, line))
    {
      if (line.rfind("SENSOR accepted:", 0) == 0)
      {
        accepted.push_back(line);
      }
      if (line.rfind("Controller state:", 0) == 0)
      {
        ++stateLines;
      }
    }

    require(accepted.size() == messages.size(),
            "Expected exactly five sensor acceptance lines\n" + stdoutText);
    for (const auto& message : messages)
    {
      require(std::count(accepted.begin(), accepted.end(),
                "SENSOR accepted: " + message) == 1,
              "Expected one acceptance for " + message + "\n" + stdoutText);
    }
    require(stateLines == 5, "Expected five controller state lines\n" + stdoutText);

    std::cout << "[TEST 28] Runtime lifecycle PASS: five sensor acceptances, "
              << "five state lines, stale fault, alive beyond 10 seconds, SIGTERM exit code 0\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "[TEST 28] FAIL: " << error.what() << '\n';
    return 1;
  }
}
