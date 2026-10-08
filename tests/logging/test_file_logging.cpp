#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "equipment_controller.hpp"
#include "logger.hpp"

namespace
{
  void require(bool condition, const std::string& message)
  {
    if (!condition)
    {
      throw std::runtime_error(message);
    }
  }

  class TemporaryLog
  {
  public:
    TemporaryLog()
    {
      const std::string pattern =
        (std::filesystem::temp_directory_path() / "equipment-log-XXXXXX").string();
      std::vector<char> name(pattern.begin(), pattern.end());
      name.push_back('\0');
      path_ = pattern;
      const int fd = mkstemp(name.data());
      require(fd >= 0, "Cannot create temporary log");
      close(fd);
      path_.assign(name.data(), pattern.size());
    }

    TemporaryLog(const TemporaryLog&) = delete;
    TemporaryLog& operator=(const TemporaryLog&) = delete;

    ~TemporaryLog()
    {
      unlink(path_.c_str());
    }

    const std::string& path() const
    {
      return path_;
    }

  private:
    std::string path_;
  };
}

int main()
{
  try
  {
    TemporaryLog file;
    {
      Logger logger(file.path());
      logger.log(LogLevel::Info, "Logger initialized");
      logger.log(LogLevel::Warning, "Warning test");
      logger.log(LogLevel::Error, "Error test");

      EquipmentController controller;
      controller.setLogger(&logger);
      controller.handleCommand(Command{CommandType::Start});
      for (int i = 0; i < 6; ++i)
      {
        controller.update();
      }
      controller.handleCommand(Command{CommandType::Stop});
      controller.handleCommand(Command{CommandType::Start});
      for (int i = 0; i < 6; ++i)
      {
        controller.update();
      }

      SensorState sensor;
      sensor.emergency_stop = true;
      controller.setSensorState(sensor);
      controller.update();
      // Match the existing repeated-fault scenario, now checking the file sink.
      controller.update();
      controller.update();
      sensor.emergency_stop = false;
      controller.setSensorState(sensor);
      controller.handleCommand(Command{CommandType::Reset});
    }

    const std::vector<std::string> expected =
    {
      "[INFO] Logger initialized",
      "[WARNING] Warning test",
      "[ERROR] Error test",
      "[INFO] START accepted: IDLE -> READY",
      "[INFO] STOP accepted: -> IDLE",
      "[INFO] START accepted: IDLE -> READY",
      "[ERROR] FAULT detected: alarm=5",
      "[INFO] RESET accepted: ERROR -> IDLE"
    };

    std::ifstream input(file.path());
    require(input.is_open(), "Cannot read log file");
    const std::regex timestamp(
      R"(^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] )"
    );
    std::vector<std::string> actual;
    std::string line;
    while (std::getline(input, line))
    {
      std::smatch match;
      require(std::regex_search(line, match, timestamp),
              "Missing timestamp in file: " + line);
      actual.push_back(line.substr(match.length()));
    }
    require(!input.bad(), "Log file read failed");
    require(actual == expected,
            "Log file level/message/order/count mismatch (expected 8 lines, got " +
            std::to_string(actual.size()) + ")");

    std::cout << "[TEST 29] File logging PASS: levels, event order, "
              << "exact counts and one fault event\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "[TEST 29] FAIL: " << error.what() << '\n';
    return 1;
  }
}
