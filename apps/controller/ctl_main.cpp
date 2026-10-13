#include "control_ipc.hpp"
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  int fd;
  try { fd = control_ipc::connectSocket(control_ipc::socketPath()); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
  if (fd < 0) {
    std::cerr << "Controller runtime is not running or IPC endpoint is unavailable.\n";
    return 1;
  }
  std::cout << "Equipment Controller CLI\nConnected.\nctl> " << std::flush;
  std::string input, output, response;
  bool waiting = false, oversized = false;
  int result = 0;
  while (true) {
    pollfd fds[] = {{fd, static_cast<short>(POLLIN | (output.empty() ? 0 : POLLOUT)), 0},
                    {STDIN_FILENO, static_cast<short>(waiting ? 0 : POLLIN), 0}};
    // Disable stdin entirely while waiting, including EOF/HUP on piped input.
    if (waiting) fds[1].fd = -1;
    if (poll(fds, 2, -1) < 0) {
      if (errno == EINTR) continue;
      result = 1; break;
    }
    if (fds[0].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
      char buffer[1024];
      auto n = recv(fd, buffer, sizeof(buffer), 0);
      if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        std::cerr << "\nController runtime connection closed.\n";
        result = 1; break;
      }
      response.append(buffer, n);
      auto end = response.find('\n');
      if (end != std::string::npos) {
        std::cout << response.substr(0, end) << "\nctl> " << std::flush;
        response.erase(0, end + 1); waiting = false;
      }
      if (response.size() > 4096) { result = 1; break; }
    }
    if ((fds[0].revents & POLLOUT) && !output.empty()) {
      auto n = send(fd, output.data(), output.size(), MSG_NOSIGNAL);
      if (n > 0) output.erase(0, n);
      else if (n < 0 && errno != EAGAIN && errno != EINTR) {
        std::cerr << "\nController runtime connection closed.\n";
        result = 1; break;
      }
    }
    if (!waiting && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
      char c;
      auto n = read(STDIN_FILENO, &c, 1);
      if (n == 0) break;
      if (n < 0) { if (errno == EINTR) continue; result = 1; break; }
      if (c != '\n') {
        if (input.size() < 256) input += c;
        else oversized = true;
        continue;
      }
      const auto command = control_ipc::normalize(input);
      input.clear();
      if (!oversized && command == "exit") break;
      if (!oversized && command == "help") std::cout << control_ipc::Help << '\n';
      else if (!oversized && (command == "start" ||
               command == "reset" || command == "status")) {
        output = command + "\n"; waiting = true;
      } else std::cout << "ERROR: invalid command; use help\n";
      oversized = false;
      if (!waiting) std::cout << "ctl> " << std::flush;
    }
  }
  close(fd);
  return result;
}
