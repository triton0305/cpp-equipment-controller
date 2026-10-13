#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <poll.h>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <memory>
#include "uart_diagnostics.hpp"

#include "controller_runtime.hpp"
#include "control_ipc.hpp"

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

  // Allocate and touch the bounded flight recorder before opening UART.
  const char* diagnostic_path = std::getenv("EQUIPMENT_UART_DIAG");
  std::unique_ptr<UartDiagnostics> diagnostics;
  if (diagnostic_path && *diagnostic_path) diagnostics = std::make_unique<UartDiagnostics>();

  std::unique_ptr<control_ipc::Server> ipc;
  try { ipc = std::make_unique<control_ipc::Server>(control_ipc::socketPath()); }
  catch (const std::exception& e) { std::cerr << "IPC: " << e.what() << '\n'; return 1; }

  EquipmentController controller;
  ControllerRuntime runtime(argv[1], controller);
  runtime.setDiagnostics(diagnostics.get());
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
          runtime.handleControlRequest(input);
          input.clear();
        }
        else if (buffer[i] != '\r' && input.size() < 256) input += buffer[i];
      }
    }
    ipc->poll([&](const std::string& request) { return runtime.handleControlRequest(request); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  runtime.shutdown();
  // No CSV formatting or disk writes while controlling equipment.
  if (diagnostics)
  {
    if (diagnostics->save(diagnostic_path)) std::cerr << "UART_DIAG saved: " << diagnostic_path << '\n';
    else std::cerr << "UART_DIAG export failed: " << diagnostic_path << '\n';
  }
  return 0;
}
