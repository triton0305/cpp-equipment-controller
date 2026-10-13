#include "runtime_fixture.hpp"
#include "control_ipc.hpp"
#include <sys/socket.h>
#include <sys/un.h>

static const std::string normal =
  "DOOR:CLOSED\nESTOP:OFF\nPRESSURE:1500\nTEMP:25\nHUMIDITY:45\nDOOR:CLOSED\nESTOP:OFF\n";

int main() {
  try {
    Fixture f;
    const auto path = f.directory + "/control.sock";
    CHECK(control_ipc::connectSocket(path) < 0);
    CHECK(control_ipc::normalize("StArT\r") == "start");
    {
      control_ipc::Server server(path);
      bool refused = false;
      try { control_ipc::Server duplicate(path); } catch (...) { refused = true; }
      CHECK(refused);
      int client = control_ipc::connectSocket(path); CHECK(client >= 0);
      auto handle = [&](const std::string& s) { return f.runtime->handleControlRequest(s); };
      auto request = [&](const std::string& command) {
        const auto wire = command + "\n";
        CHECK(send(client, wire.data(), wire.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(wire.size()));
        std::string response;
        for (int i = 0; i < 100 && response.find('\n') == std::string::npos; ++i) {
          server.poll(handle);
          char buf[2048]; auto n = recv(client, buf, sizeof(buf), 0);
          if (n > 0) response.append(buf, n);
        }
        CHECK(!response.empty()); return response;
      };
      CHECK(request("start").find("REJECTED") == 0);
      CHECK(request("bogus").find("ERROR") == 0);
      f.runtime->poll(); f.endpoint->drain();
      for (unsigned t = 0; t <= 5000; t += 100) {
        f.now = ControllerRuntime::Clock::time_point{} + std::chrono::milliseconds(t);
        f.receive(normal); f.endpoint->drain();
      }
      CHECK(request("status").find("Ready=1") != std::string::npos);
      CHECK(f.endpoint->drain().empty()); // snapshot causes no UART writes
      CHECK(request("START\r") == "OK\n");
      CHECK(f.core.getState() == EquipmentState::Run);
      close(client); server.poll(handle);
      CHECK(f.core.getState() == EquipmentState::Run);
      f.runtime->poll(); CHECK(f.core.getState() == EquipmentState::Run);
      client = control_ipc::connectSocket(path); CHECK(client >= 0);
      CHECK(request("stop") == "OK\n");
      CHECK(f.core.getState() == EquipmentState::Idle); safe(f.core);
      f.receive("ESTOP:ON\n");
      CHECK(request("reset").find("REJECTED") == 0);
      f.receive(normal);
      CHECK(request("reset") == "OK\n");
      CHECK(f.core.getState() == EquipmentState::Idle);
      // stdin uses exactly this dispatcher; compare no-op rejection and status.
      CHECK(request("start") == f.runtime->handleControlRequest("start") + "\n");
      CHECK(request("status") == f.runtime->statusSnapshot() + "\n");
      // Fragmentation and queued lines must preserve framing.
      CHECK(send(client, "sta", 3, 0) == 3); server.poll(handle);
      char b[4096]; CHECK(recv(client, b, sizeof(b), 0) < 0);
      CHECK(send(client, "tus\ninvalid\n", 12, 0) == 12);
      server.poll(handle); CHECK(recv(client, b, sizeof(b), 0) > 0);
      server.poll(handle); auto n = recv(client, b, sizeof(b), 0);
      CHECK(n > 0 && std::string(b, n).find("ERROR") == 0);
      // A reader that stops consuming replies cannot block service.
      for (int i = 0; i < 10000; ++i) {
        send(client, "status\n", 7, MSG_NOSIGNAL | MSG_DONTWAIT);
        server.poll(handle);
      }
      close(client);
    }
    CHECK(access(path.c_str(), F_OK) < 0);
    // Simulate crash residue without an active listener.
    int stale = socket(AF_UNIX, SOCK_STREAM, 0); CHECK(stale >= 0);
    sockaddr_un a{}; a.sun_family = AF_UNIX;
    std::copy(path.begin(), path.end(), a.sun_path);
    CHECK(bind(stale, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0); close(stale);
    { control_ipc::Server recovered(path); }
    unlink((path + ".lock").c_str());
    std::cout << "IPC host checks passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
