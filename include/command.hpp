#ifndef COMMAND_HPP
#define COMMAND_HPP

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

#endif