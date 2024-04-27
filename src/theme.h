#ifndef __K_THEME_H__
#define __K_THEME_H__

#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class ThemeConfig {
private:
  static ThemeConfig *instance;
  std::string path;

protected:
  json data;

public:
  ThemeConfig();
  ThemeConfig(ThemeConfig &o) = delete;
  void operator=(const ThemeConfig &) = delete;
  void init(const std::string config_path);

  template<typename T> T get(const std::string &json_ptr) {
    return data[json::json_pointer(json_ptr)].template get<T>();
  };

  template<typename T> T set(const std::string &json_ptr, T v) {
    return data[json::json_pointer(json_ptr)] = v;
  };

  json &get_json(const std::string &json_path);

  void save();

  static ThemeConfig *get_instance();

};

#endif // __K_THEME_H__
