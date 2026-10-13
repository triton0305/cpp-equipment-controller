#pragma once
#include <functional>
#include <string>

namespace control_ipc {
std::string socketPath();
int connectSocket(const std::string& path);
std::string normalize(std::string text);
constexpr const char* Help = "start reset status help exit";

// Serviced only by the runtime thread; one bounded request per poll.
class Server {
public:
  explicit Server(const std::string& path);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  void poll(const std::function<std::string(const std::string&)>& handle);
private:
  void disconnect();
  std::string path_, input_, output_;
  int listener_ = -1, client_ = -1, lock_ = -1;
  bool bound_ = false;
};
}
