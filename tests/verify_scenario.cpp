
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>

namespace
{
  std::string readFile(const std::string& path)
  {
    std::ifstream file(path);
    if (!file)
    {
      throw std::runtime_error("Cannot open baseline: " + path);
    }

    std::ostringstream stream;
    stream << file.rdbuf();

    if (file.bad())
    {
      throw std::runtime_error("Cannot read baseline: " + path);
    }

    return stream.str();
  }

  std::string trim(const std::string& text)
  {
    const auto first = text.find_first_not_of(" \t\n\r");

    if (first == std::string::npos)
    {
      return "";
    }

    const auto last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
  }

  std::string scenario(const std::string& text, int number)
  {
    const std::regex marker(
      "^\\[TEST " + std::to_string(number) + "\\] [^\\r\\n]*$",
      std::regex_constants::multiline
    );

    std::smatch match;

    if (!std::regex_search(text, match, marker))
    {
      throw std::runtime_error(
        "TEST " + std::to_string(number) + " missing"
      );
    }

    const std::size_t start = static_cast<std::size_t>(
      match.position() + match.length()
    );

    const std::string remaining = text.substr(start);

    const std::regex next(
      "^\\[TEST [0-9]+\\] ",
      std::regex_constants::multiline
    );

    std::smatch nextMatch;

    if (std::regex_search(remaining, nextMatch, next))
    {
      return trim(remaining.substr(0, nextMatch.position()));
    }

    return trim(remaining);
  }

  std::string normalized(const std::string& text)
  {
    const std::regex timestamp(
      R"(\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\])"
    );

    return std::regex_replace(text, timestamp, "[TIMESTAMP]");
  }

  std::string runProgram(const std::string& executable)
  {
    int pipefd[2];

    if (pipe(pipefd) != 0)
    {
      throw std::runtime_error("pipe failed");
    }

    const pid_t pid = fork();

    if (pid < 0)
    {
      close(pipefd[0]);
      close(pipefd[1]);
      throw std::runtime_error("fork failed");
    }

    if (pid == 0)
    {
      close(pipefd[0]);

      if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
          dup2(pipefd[1], STDERR_FILENO) < 0)
      {
        _exit(127);
      }

      close(pipefd[1]);
      execl(executable.c_str(), executable.c_str(), nullptr);
      _exit(127);
    }

    close(pipefd[1]);

    const int flags = fcntl(pipefd[0], F_GETFL, 0);

    if (flags < 0 || fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK) < 0)
    {
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
      close(pipefd[0]);
      throw std::runtime_error("fcntl failed");
    }

    std::string output;
    int status = 0;
    bool finished = false;
    bool timedOut = false;
    bool pipeClosed = false;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(15);

    while (!finished || !pipeClosed)
    {
      if (!finished)
      {
        const pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid)
        {
          finished = true;
        }
        else if (result < 0 && errno != EINTR)
        {
          kill(pid, SIGKILL);
          waitpid(pid, nullptr, 0);
          close(pipefd[0]);
          throw std::runtime_error("waitpid failed");
        }
      }

      if (!finished && std::chrono::steady_clock::now() >= deadline)
      {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        finished = true;
        timedOut = true;
      }

      if (!pipeClosed)
      {
        pollfd pfd{pipefd[0], static_cast<short>(POLLIN | POLLHUP), 0};

        const int pollResult = poll(&pfd, 1, 10);

        if (pollResult < 0)
        {
          if (errno == EINTR)
          {
            continue;
          }

          if (!finished)
          {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
          }

          close(pipefd[0]);
          throw std::runtime_error("poll failed");
        }

        if (pollResult > 0)
        {
          char buffer[4096];

          while (true)
          {
            const ssize_t bytes = read(pipefd[0], buffer, sizeof(buffer));

            if (bytes > 0)
            {
              output.append(buffer, static_cast<std::size_t>(bytes));
            }
            else if (bytes == 0)
            {
              pipeClosed = true;
              break;
            }
            else if (errno == EINTR)
            {
              continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
              break;
            }
            else
            {
              if (!finished)
              {
                kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
              }

              close(pipefd[0]);
              throw std::runtime_error("read failed");
            }
          }
        }
      }

      if (timedOut)
      {
        break;
      }
    }

    close(pipefd[0]);

    if (timedOut)
    {
      throw std::runtime_error(
        "Scenario executable timeout (15 seconds)\n" + output
      );
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
      throw std::runtime_error(
        "Scenario executable failed:\n" + output
      );
    }

    return output;
  }
}

int main(int argc, char* argv[])
{
  if (argc != 4)
  {
    std::cerr
      << "Usage: verify_scenario <number> <executable> <baseline>\n";
    return 1;
  }

  try
  {
    const int number = std::stoi(argv[1]);

    const std::string output = runProgram(argv[2]);
    const std::string baseline = readFile(argv[3]);

    const std::string expected = normalized(scenario(baseline, number));
    const std::string actual = normalized(scenario(output, number));

    if (actual != expected)
    {
      std::cerr
        << "TEST " << number << " mismatch\n"
        << "Expected:\n" << expected << '\n'
        << "Actual:\n" << actual << '\n';

      return 1;
    }

    std::cout
      << "TEST " << number
      << " passed: observed values match baseline\n";

    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
