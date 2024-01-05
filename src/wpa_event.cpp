#include "wpa_event.h"
#include "wpa_ctrl.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <memory>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

WpaEvent::WpaEvent()
  : hv::EventLoopThread(NULL)
  , conn(NULL)
{
}

WpaEvent::~WpaEvent() {
  spdlog::trace("wpa event stopped");
  stop();
}

void WpaEvent::start() {
  if (isRunning()) {
    loop()->runInLoop(std::bind(&WpaEvent::init_wpa, this));
  } else {
    hv::EventLoopThread::start(true, [this]() {
      WpaEvent::init_wpa();
      return 0;
    });
  }
}

void WpaEvent::stop() {
  spdlog::trace("wpa event stopped1");  
  hv::EventLoopThread::stop(true);
}

void WpaEvent::register_callback(const std::string &name,
				 std::function<void(const std::string&)> cb) {
  const auto &entry = callbacks.find(name);
  if (entry == callbacks.end()) {
    callbacks.insert({name, cb});
  } else {
    /// XXX: replace callback?
  }
}

void WpaEvent::init_wpa() {
  // TODO: retries
  
  std::string wpa_socket = Config::get_instance()->get<std::string>("/wpa_supplicant");
  if (fs::is_directory(fs::status(wpa_socket))) {
    for (const auto &e : fs::directory_iterator(wpa_socket)) {
      if (fs::is_socket(e.path())
	  && e.path().string().find("p2p") == std::string::npos) {
	spdlog::debug("found wpa supplicant socket {}", e.path().string());
	wpa_socket = e.path().string();
	break;
      }
    }
  }
  
  if (conn == NULL) {
    conn = wpa_ctrl_open(wpa_socket.c_str());
    if (conn == NULL) {
      spdlog::trace("failed to open wpa control");
      return;
    }
  }
  
  struct wpa_ctrl *mon_conn = wpa_ctrl_open(wpa_socket.c_str());

  if (mon_conn != NULL) {
    if (wpa_ctrl_attach(mon_conn) == 0) {
      spdlog::trace("attached to wpa supplicant");
    }
  } else {
    spdlog::trace("failed to attached to wpa supplicant");
    return;
  }

  int monfd = wpa_ctrl_get_fd(mon_conn);
  hio_t* io = hio_get(loop()->loop(), monfd);
  spdlog::trace("set io fd {}", monfd);
  
  if (io == NULL) {
    spdlog::trace("failed to poll wpa supplicant monitor socket");
  }
  hio_set_context(io, this);
  hio_setcb_read(io, WpaEvent::_handle_wpa_events);
  hio_read_start(io);
  spdlog::trace("registered io read callback");
  
}

void WpaEvent::handle_wpa_events(void *data, int len) {
  std::string event = std::string((char*)data, len);
  spdlog::trace("handling wpa event {}", event);
  for (const auto &entry : callbacks) {
    entry.second(event);
  }
}

std::string WpaEvent::send_command(const std::string &cmd) {
  char resp[4096];
  size_t len = sizeof(resp) -1;
  if (conn != NULL) {
    spdlog::trace("sending cmd {}", cmd);
    if (wpa_ctrl_request(conn, cmd.c_str(), cmd.length(), resp, &len, NULL) == 0) {
      return std::string(resp, len);
    }
    spdlog::trace("failed to send cmd {} to wpa supplicant", cmd);
  }

  return "";
}
