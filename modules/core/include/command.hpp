#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <string>

enum class CommandType
{
  Start,
  Stop,
  Reset,
  Status,
  Quit,
  Invalid
};

struct Command
{
  CommandType type = CommandType::Invalid;
};

inline Command parseCommand(std::string text)
{
  for (char& c : text) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  if (text == "START") return {CommandType::Start};
  if (text == "STOP") return {CommandType::Stop};
  if (text == "RESET") return {CommandType::Reset};
  return {CommandType::Invalid};
}

#endif