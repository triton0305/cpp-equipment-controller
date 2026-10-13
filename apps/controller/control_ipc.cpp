#include "control_ipc.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace control_ipc {
namespace {
sockaddr_un address(const std::string& path) {
  sockaddr_un a{};
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof(a.sun_path)) throw std::runtime_error("IPC socket path too long");
  std::memcpy(a.sun_path, path.c_str(), path.size() + 1);
  return a;
}
bool retryable() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
}
std::string normalize(std::string text) {
  if (!text.empty() && text.back() == '\r') text.pop_back();
  for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return text;
}
std::string socketPath() {
  if (const char* p = std::getenv("EQUIPMENT_CONTROL_SOCKET")) {
    if (*p) return p;
  }
  const auto dir = "/tmp/equipment-controller-" + std::to_string(getuid());
  if (mkdir(dir.c_str(), 0700) < 0 && errno != EEXIST)
    throw std::runtime_error("Cannot create IPC directory");
  struct stat st{};
  if (lstat(dir.c_str(), &st) < 0 || !S_ISDIR(st.st_mode) ||
      st.st_uid != getuid() || (st.st_mode & 0077))
    throw std::runtime_error("IPC directory must be owned by current user and private");
  return dir + "/control.sock";
}
int connectSocket(const std::string& path) {
  const auto a = address(path);
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  if (connect(fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) == 0) return fd;
  const int error = errno;
  close(fd); errno = error; return -1;
}
Server::Server(const std::string& path) : path_(path) {
  const auto a = address(path);
  try {
    lock_ = open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0 || flock(lock_, LOCK_EX | LOCK_NB) < 0)
      throw std::runtime_error("IPC endpoint already owned or lock unavailable");
    struct stat st{};
    if (lstat(path.c_str(), &st) == 0) {
      if (!S_ISSOCK(st.st_mode) || st.st_uid != getuid())
        throw std::runtime_error("Refusing to remove non-owned socket entry");
      int probe = connectSocket(path);
      if (probe >= 0) { close(probe); throw std::runtime_error("Controller runtime already running"); }
      if (errno != ECONNREFUSED) throw std::runtime_error("IPC endpoint cannot be safely replaced");
      if (unlink(path.c_str()) < 0) throw std::runtime_error("Cannot remove stale IPC socket");
    } else if (errno != ENOENT) throw std::runtime_error("Cannot inspect IPC socket");
    listener_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener_ < 0 || bind(listener_, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) < 0)
      throw std::runtime_error("Cannot bind IPC socket");
    bound_ = true;
    if (chmod(path.c_str(), 0600) < 0 || listen(listener_, 1) < 0)
      throw std::runtime_error("Cannot listen on IPC socket");
  } catch (...) {
    if (listener_ >= 0) close(listener_);
    if (bound_) unlink(path_.c_str());
    if (lock_ >= 0) close(lock_);
    throw;
  }
}
Server::~Server() {
  disconnect();
  if (listener_ >= 0) close(listener_);
  if (bound_) unlink(path_.c_str());
  // Keep lock inode so another process cannot lock a different inode at this path.
  if (lock_ >= 0) close(lock_);
}
void Server::disconnect() {
  if (client_ >= 0) close(client_);
  client_ = -1; input_.clear(); output_.clear();
}
void Server::poll(const std::function<std::string(const std::string&)>& handle) {
  // Drain at most one extra connection; v1 supports one client.
  int incoming = accept4(listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (incoming >= 0) {
    if (client_ < 0) client_ = incoming;
    else close(incoming);
  }
  if (client_ < 0) return;
  if (output_.empty()) {
    if (input_.find('\n') == std::string::npos) {
      char buffer[256];
      const auto n = recv(client_, buffer, sizeof(buffer), 0);
      if (n == 0 || (n < 0 && !retryable())) { disconnect(); return; }
      if (n > 0) input_.append(buffer, n);
    }
    const auto end = input_.find('\n');
    if (end != std::string::npos && end <= 256) {
      output_ = handle(normalize(input_.substr(0, end))) + "\n";
      input_.erase(0, end + 1);
    } else if (input_.size() > 256) { disconnect(); return; }
  }
  if (!output_.empty()) {
    const auto n = send(client_, output_.data(), output_.size(), MSG_NOSIGNAL);
    if (n > 0) output_.erase(0, n);
    else if (n < 0 && !retryable()) disconnect();
  }
}
}
