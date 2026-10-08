#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <fstream>
#include <mutex>
#include <string>

enum class LogLevel
{
  Info,
  Warning,
  Error
};

class Logger
{
public:
  explicit Logger(const std::string& file_path);

  void log(LogLevel level, const std::string& message);

private:
  std::ofstream file_;
  std::mutex mutex_;
};

#endif