#ifndef __WPA_EVENT_H__
#define __WPA_EVENT_H__

#include "hv/hloop.h"
#include "hv/EventLoop.h"
#include "hv/EventLoopThread.h"

#include <map>
#include <functional>

class WpaEvent : private hv::EventLoopThread {
 public:
  WpaEvent();
  ~WpaEvent();

  void start();
  void stop();

  void register_callback(const std::string &name, std::function<void(const std::string&)>);
  void init_wpa();
  void handle_wpa_events(void *data, int len);
  std::string send_command(const std::string &cmd);

  static void _handle_wpa_events(hio_t *io, void *data, int readbyte) {
    WpaEvent* wpa_event = (WpaEvent*)hio_context(io);
    wpa_event->handle_wpa_events(data, readbyte);
  }

 private:
  struct wpa_ctrl *conn;
  std::map<std::string, std::function<void(const std::string&)>> callbacks;
};

#endif // __WPA_EVENT_H__
