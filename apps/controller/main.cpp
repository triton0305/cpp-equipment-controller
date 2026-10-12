#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <poll.h>
#include <unistd.h>
#include <string>

#include "controller_runtime.hpp"

namespace
{
  volatile std::sig_atomic_t stopping = 0;
  void stop(int) { stopping = 1; }
}

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0] << " <serial_device>\n";
    return 1;
  }
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  std::cout << std::unitbuf;

  EquipmentController controller;
  ControllerRuntime runtime(argv[1], controller);
  std::string input;
  bool stdin_open = true;
  while (!stopping)
  {
    runtime.poll();
    pollfd command_input{STDIN_FILENO, POLLIN, 0};
    if (stdin_open && ::poll(&command_input, 1, 0) > 0)
    {
      char buffer[256];
      const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (count <= 0) stdin_open = false;
      for (ssize_t i = 0; i < count; ++i)
      {
        if (buffer[i] == '\n')
        {
          const auto command = parseCommand(input);
          if (command.type != CommandType::Invalid) runtime.handleCommand(command);
          input.clear();
        }
        else if (buffer[i] != '\r' && input.size() < 256) input += buffer[i];
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.shutdown();
  return 0;
}
