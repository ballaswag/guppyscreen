#include "config.h"

#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

Config *Config::instance{NULL};

Config::Config() {
}

Config *Config::get_instance() {
  if (instance == NULL) {
    instance = new Config();
  }
  return instance;
}

void Config::init(const std::string config_path) {
  struct stat buffer;
  json fans_conf = {
    {
      {"id", "output_pin fan0"},
      {"display_name", "Toolhead Fan"}
    },
    {
      {"id", "output_pin fan1"},
      {"display_name", "Back Fan"}
    },
    {
      {"id", "output_pin fan2"},
      {"display_name", "Side Fan"}
    }
  };

  json sensors_conf = {
    {
      {"id", "extruder"},
      {"display_name", "Extruder"},
      {"controllable", true},
      {"color", "red"}
    },
    {
      {"id", "heater_bed"},
      {"display_name", "Bed"},
      {"controllable", true},
      {"color", "purple"}
    },
    {
      {"id", "temperature_sensor chamber_temp"},
      {"display_name", "Chamber"},
      {"controllable", false},
      {"color", "blue"}
    }
  };

  json cooldown_conf = {{ "cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0"}};
  json default_macros_conf = {
    {"load_filament", "LOAD_MATERIAL"},
    {"unload_filament", "QUIT_MATERIAL"}
  };

  if (stat(config_path.c_str(), &buffer) == 0) {
    data = json::parse(std::fstream(config_path));

  } else {
    data = {
      {"default_printer", "k1"},
      {"log_path", "/usr/data/printer_data/logs/guppyscreen.log"},
      {"thumbnail_path", "/usr/data/printer_data/thumbnails"},
      {"wpa_supplicant", "/var/run/wpa_supplicant/wlan0"},
      {"printers", {{
	    "k1", {
	      {"moonraker_api_key", false},
	      {"moonraker_host", "127.0.0.1"},
	      {"moonraker_port", 7125},
	      {"display_sleep_sec", 600},
	      {"monitored_sensors", sensors_conf},
	      {"fans", fans_conf},
	      {"default_macros", default_macros_conf}
	    }
	  }}
      }
    };

  }
  
  std::string df_name = data["/default_printer"_json_pointer];
  default_printer = "/printers/" + df_name + "/";

  auto &monitored_sensors = data[json::json_pointer(df() + "monitored_sensors")];
  if (monitored_sensors.is_null()) {
    data[json::json_pointer(df() + "monitored_sensors")] = sensors_conf;
  }

  auto &fans = data[json::json_pointer(df() + "fans")];
  if (fans.is_null()) {
    data[json::json_pointer(df() + "fans")] = fans_conf;
  }

  auto &default_macros = data[json::json_pointer(df() + "default_macros")];
  if (default_macros.is_null()) {
    default_macros_conf.merge_patch(cooldown_conf);
    data[json::json_pointer(df() + "default_macros")] = default_macros_conf;
  } else {
    if (!default_macros.contains("cooldown")) {
      default_macros.merge_patch(cooldown_conf);
    }
  }

  auto &guppy_init = data[json::json_pointer(df() + "guppy_init_script")];
  if (guppy_init.is_null()) {
    data[json::json_pointer(df() + "guppy_init_script")] = "/etc/init.d/S99guppyscreen";
  }
  

  std::ofstream o(config_path);
  o << std::setw(2) << data << std::endl;
}

std::string& Config::df() {
  return default_printer;
}

std::string Config::get_thumbnail_path() {
  return get<std::string>("/thumbnail_path");
}

std::string Config::get_wifi_interface() {
  return fs::path(get<std::string>("/wpa_supplicant"))
    .filename()
    .string();
}

json &Config::get_json(const std::string &json_path) {
  return data[json::json_pointer(json_path)];
}
