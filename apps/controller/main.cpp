#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

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
  while (!stopping)
  {
    runtime.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.shutdown();
  return 0;
}
