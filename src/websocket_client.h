#ifndef __KWEBSOCKET_CLIENT_H__
#define __KWEBSOCKET_CLIENT_H__

#include "hv/WebSocketClient.h"
#include "notify_consumer.h"
#include "hv/json.hpp"

#include <map>
#include <vector>
#include <atomic>
#include <functional>

using json = nlohmann::json;

class KWebSocketClient : public hv::WebSocketClient {
 public:
  KWebSocketClient(hv::EventLoopPtr loop);
  ~KWebSocketClient();

  int connect(const char* url,
	      std::function<void()> connected,
	      std::function<void()> disconnected);

  void register_notify_update(NotifyConsumer *consumer);
  void unregister_notify_update(NotifyConsumer *consumer);

  // void register_gcode_resp(std::function<void(json&)> cb);

  int send_jsonrpc(const std::string &method, std::function<void(json&)> cb);
  int send_jsonrpc(const std::string &method, const json &params, std::function<void(json&)> cb);  
  int send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer);  
  int send_jsonrpc(const std::string &method, const json &params);
  int send_jsonrpc(const std::string &method);
  int gcode_script(const std::string &gcode);

  void register_method_callback(std::string resp_method,
				std::string handler_name,
				std::function<void(json&)> cb);
  
 private:
  std::map<uint32_t, std::function<void(json&)>> callbacks;
  std::map<uint32_t, NotifyConsumer*> consumers;
  std::vector<NotifyConsumer*> notify_consumers;
  // std::vector<std::function<void(json&)>> gcode_resp_cbs;

  // method_name : { <unique-name-cb-handler> :handler-cb }
  std::map<std::string, std::map<std::string, std::function<void(json&)>>> method_resp_cbs;
  std::atomic_uint64_t id;
};

#endif //__KWEBSOCKET_CLIENT_H__
