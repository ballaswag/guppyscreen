/*
 * websocket client
 *
 * @build   make examples
 * @server  bin/websocket_server_test 8888
 * @client  bin/websocket_client_test ws://127.0.0.1:8888/test
 * @clients bin/websocket_client_test ws://127.0.0.1:8888/test 100
 * @python  scripts/websocket_server.py
 * @js      html/websocket_client.html
 *
 */

#include "websocket_client.h"
#include "spdlog/spdlog.h"

#include <algorithm>

using namespace hv;
using json = nlohmann::json;

KWebSocketClient::KWebSocketClient(EventLoopPtr loop)
  : WebSocketClient(loop)
  , id(0)
{
}

KWebSocketClient::~KWebSocketClient() {
}

int KWebSocketClient::connect(const char* url,
			      std::function<void()> connected,
			      std::function<void()> disconnected) {
  spdlog::debug("websocket connecting");
  // set callbacks
  onopen = [this, connected]() {
    const HttpResponsePtr& resp = getHttpResponse();
    spdlog::debug("onopen {}", resp->body.c_str());
    connected();
  };
  onmessage = [this, connected, disconnected](const std::string &msg) {
    // if (msg.find("notify_proc_stat_update") == std::string::npos) {
    //   spdlog::trace("onmessage(type={} len={}): {}", opcode() == WS_OPCODE_TEXT ? "text" : "binary",
    // 	     (int)msg.size(), msg);
    // }
    auto j = json::parse(msg);

    if (j.contains("id")) {
      // XXX: get rid of consumers and use function ptrs for callback
      const auto &entry = consumers.find(j["id"]);
      if (entry != consumers.end()) {
        entry->second->consume(j);
        consumers.erase(entry);
      }

      const auto &cb_entry = callbacks.find(j["id"]);
      if (cb_entry != callbacks.end()) {
        cb_entry->second(j);
        callbacks.erase(cb_entry);
      }
    }

    if (j.contains("method")) {
      std::string method = j["method"].template get<std::string>();
      if ("notify_status_update" == method || "notify_filelist_changed" == method) {
        for (const auto &entry : notify_consumers) {
          entry->consume(j);
        }
      }  //  else if ("notify_gcode_response" == method) {
      // 	for (const auto &gcode_cb : gcode_resp_cbs) {
      // 	  gcode_cb(j);
      // 	}
      // }
      else if ("notify_klippy_disconnected" == method) {
        disconnected();
      } else if ("notify_klippy_ready" == method) {
        connected();
      }

      for (const auto &entry : method_resp_cbs) {
        if (method == entry.first) {
          for (const auto &handler_entry : entry.second) {
            handler_entry.second(j);
          }
        }
      }
    }
  };

  onclose = [disconnected]() {
    spdlog::debug("onclose");
    disconnected();
  };

  // ping
  setPingInterval(10000);

  reconn_setting_t reconn;
  reconn_setting_init(&reconn);
  reconn.min_delay = 200;
  reconn.max_delay = 2000;
  reconn.delay_policy = 2;
  setReconnect(&reconn);

  http_headers headers;
  return open(url, headers);
};

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   const json &params,
				   std::function<void(json&)> cb) {
  const auto &entry = callbacks.find(id);
  if (entry == callbacks.end()) {
    // spdlog::debug("registering consume %d, %x\n", id, consumer);
    callbacks.insert({id, cb});
    // XXX: check success, remove consumer if send is unsuccessfull
    return send_jsonrpc(method, params);
  } else {
    // spdlog::debug("WARN: id %d is already register with a consumer\n", id);
  }

  return 0;
}

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   std::function<void(json&)> cb) {
  const auto &entry = callbacks.find(id);
  if (entry == callbacks.end()) {
    // spdlog::debug("registering consume %d, %x\n", id, consumer);
    callbacks.insert({id, cb});
    // XXX: check success, remove consumer if send is unsuccessfull
    return send_jsonrpc(method);
  } else {
    // spdlog::debug("WARN: id %d is already register with a consumer\n", id);
  }

  return 0;
}

int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer) {
  const auto &entry = consumers.find(id);
  if (entry == consumers.end()) {
    consumers.insert({id, consumer});
    return send_jsonrpc(method, params);
  }
  return 0;
}

void KWebSocketClient::register_notify_update(NotifyConsumer *consumer) {
  if (std::find(notify_consumers.begin(), notify_consumers.end(), consumer) == std::end(notify_consumers)) {
    notify_consumers.push_back(consumer);
  }
}

void KWebSocketClient::unregister_notify_update(NotifyConsumer *consumer) {
  notify_consumers.erase(std::remove_if(
    notify_consumers.begin(), notify_consumers.end(),
    [consumer](NotifyConsumer *c) {
      return c == consumer;
    }));
}

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   const json &params) {
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  rpc["params"] = params;
  rpc["id"] = id++;

  spdlog::debug("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int KWebSocketClient::send_jsonrpc(const std::string &method) {
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  rpc["id"] = id++;

  spdlog::debug("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int KWebSocketClient::gcode_script(const std::string &gcode) {
  json cmd = {{ "script", gcode }};
  return send_jsonrpc("printer.gcode.script", cmd);
}

void KWebSocketClient::register_method_callback(std::string resp_method,
						std::string handler_name,
						std::function<void(json&)> cb) {

  const auto &entry = method_resp_cbs.find(resp_method);
  if (entry == method_resp_cbs.end()) {
    spdlog::debug("registering new method {}, handler {}", resp_method, handler_name);
    std::map<std::string, std::function<void(json&)>> handler_map;
    handler_map.insert({handler_name, cb});
    method_resp_cbs.insert({resp_method, handler_map});
  } else {
    spdlog::debug("found existing resp_method {} with handlers, updating handler callback {}",
		  resp_method, handler_name);
    entry->second.insert({handler_name, cb});
  }
}
