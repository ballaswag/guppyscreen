#ifndef __K_CONFIG_H__
#define __K_CONFIG_H__

#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class Config {
 private:
  static Config *instance;

 protected:
  json data;
  std::string default_printer;

 public:
  Config();
  Config(Config &o) = delete;
  void operator=(const Config &) = delete;
  void init(const std::string config_path);

  template<typename T> T get(const std::string &json_ptr) {
    return data[json::json_pointer(json_ptr)].template get<T>();
  };

  json &get_json(const std::string &json_path);

  std::string& df();
  std::string get_thumbnail_path();
  std::string get_wifi_interface();

  static Config *get_instance();
};

#endif // __K_CONFIG_H__
