#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ascii_protocol.hpp"
#include "equipment_controller.hpp"
#include "serial_transport.hpp"

int main(int argc, char* argv[])
{
  if (argc != 2)
  {
    std::cerr << "Usage: " << argv[0]
              << " <serial_device>\n";
    return 1;
  }

  SerialTransport serial;

  if (!serial.openPort(argv[1]))
  {
    std::cerr << "Failed to open serial port: "
              << argv[1] << '\n';
    return 1;
  }

  EquipmentController controller;
  SensorState sensor = controller.getSensorState();

  const auto deadline =
    std::chrono::steady_clock::now() +
    std::chrono::seconds(10);

  while (std::chrono::steady_clock::now() < deadline)
  {
    std::vector<std::string> lines;

    if (!serial.readLines(lines))
    {
      std::cerr << "UART read failed\n";
      return 1;
    }

    for (const std::string& line : lines)
    {
      if (!AsciiProtocol::parseSensorLine(line, sensor))
      {
        std::cerr << "Invalid SENSOR line: "
                  << line << '\n';
        continue;
      }

      controller.setSensorState(sensor);
      controller.update();

      const std::string output =
        AsciiProtocol::serializeDeviceState(controller.getDeviceState());

      if (!serial.writeAll(output))
      {
          std::cerr << "UART write failed\n";
          return 1;
      }

      std::cout << "SENSOR accepted: " << line << '\n';

      std::cout << "Controller state: "
                << static_cast<int>(controller.getState())
                << " | Step: "
                << static_cast<int>(controller.getSequenceStep())
                << " | Alarm: "
                << static_cast<int>(controller.getAlarmCode())
                << '\n';
    }

    std::this_thread::sleep_for(
      std::chrono::milliseconds(10)
    );
  }

  serial.closePort();
  return 0;
}