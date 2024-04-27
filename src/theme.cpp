#include "theme.h"
#include "platform.h"

#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

ThemeConfig *ThemeConfig::instance{NULL};

ThemeConfig::ThemeConfig() {
}

ThemeConfig *ThemeConfig::get_instance() {
  if (instance == NULL) {
    instance = new ThemeConfig();
  }
  return instance;
}

void ThemeConfig::init(const std::string config_path) {
  path = config_path;
  struct stat buffer;

  if (stat(config_path.c_str(), &buffer) == 0) {
    data = json::parse(std::fstream(config_path));
  } else {
    data = {
        {"primary_color", "0x2196F3"}, //blue
        {"secondary_color", "0xF44336"} // red
    };
  }

  std::ofstream o(config_path);
  o << std::setw(2) << data << std::endl;
}

json &ThemeConfig::get_json(const std::string &json_path) {
  return data[json::json_pointer(json_path)];
}

void ThemeConfig::save() {
  std::ofstream o(path);
  o << std::setw(2) << data << std::endl;
}
