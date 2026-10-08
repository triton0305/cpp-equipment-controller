#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
const char* levelToString(LogLevel level)
{
  switch (level)
  {
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error:   return "ERROR";
  }

  return "UNKNOWN";
}
}

Logger::Logger(const std::string& file_path)
  : file_(file_path, std::ios::app)
{
  if (!file_)
  {
    std::cerr << "Failed to open log file: "
              << file_path << '\n';
  }
}

void Logger::log(LogLevel level, const std::string& message)
{
  std::lock_guard<std::mutex> lock(mutex_);

  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);

  std::tm local_time{};

  if (const std::tm* time_info = std::localtime(&time))
  {
    local_time = *time_info;
  }

  std::ostringstream line;

  line << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
       << "] [" << levelToString(level) << "] "
       << message;

  std::cout << line.str() << '\n';

  if (file_)
  {
    file_ << line.str() << '\n';
    file_.flush();
  }
}