#include "config.h"
#include "platform.h"

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

void Config::init(std::string config_path, const std::string thumbdir) {
  path = config_path;
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
    {"load_filament", "_GUPPY_LOAD_MATERIAL"},
    {"unload_filament", "_GUPPY_QUIT_MATERIAL"}
  };

  if (stat(config_path.c_str(), &buffer) == 0) {
    data = json::parse(std::fstream(config_path));
  } else {
    data = {
        {"log_path", "/usr/data/printer_data/logs/guppyscreen.log"},
        {"thumbnail_path", thumbdir},
        {"wpa_supplicant", "/var/run/wpa_supplicant"},
        {"display_sleep_sec", 600}
#ifndef OS_ANDROID
        , {"default_printer", "k1"},
        {"printers", {{"k1", {
                                 {"moonraker_api_key", false},
                                 {"moonraker_host", "127.0.0.1"},
                                 {"moonraker_port", 7125},
                                 {"monitored_sensors", sensors_conf},
                                 {"fans", fans_conf},
                                 {"default_macros", default_macros_conf},
                             }}}
        }
#endif
    };
  }

  data["config_path"] = config_path;

  auto df_name = data["/default_printer"_json_pointer];
  if (!df_name.is_null()) {
    default_printer = "/printers/" + df_name.template get<std::string>() + "/";

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

    auto &guppy_init = data["/guppy_init_script"_json_pointer];
    if (guppy_init.is_null()) {
      data["/guppy_init_script"_json_pointer] = "/etc/init.d/S99guppyscreen";
    }

    auto &ll = data[json::json_pointer(df() + "log_level")];
    if (ll.is_null()) {
      data[json::json_pointer(df() + "log_level")] = "debug";
    }
  }
  auto &rotate = data["/display_rotate"_json_pointer];
  if (rotate.is_null()) {
#ifdef GUPPY_ROTATE
    data["/display_rotate"_json_pointer] = 3; // LV_DISP_ROT_270
#else
    data["/display_rotate"_json_pointer] = 0; // LV_DISP_ROT_0
#endif
  }

  auto &touch_calibrated = data["/touch_calibrated"_json_pointer];
  if (touch_calibrated.is_null()) {
#ifdef EVDEV_CALIBRATE
    data["/touch_calibrated"_json_pointer] = true; // EVDEV_CALIBRATE
#else
    data["/touch_calibrated"_json_pointer] = false; // EVDEV_CALIBRATE
#endif
  }

  auto &estop = data["/prompt_emergency_stop"_json_pointer];
  if (estop.is_null()) {
    data["/prompt_emergency_stop"_json_pointer] = true;
  }

  auto &display_sleep = data["/display_sleep_sec"_json_pointer];
  if (display_sleep.is_null()) {
    data["/display_sleep_sec"_json_pointer] = 600;
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

std::string Config::get_path() {
    return path;
}

json &Config::get_json(const std::string &json_path) {
  return data[json::json_pointer(json_path)];
}

void Config::save() {
  std::ofstream o(path);
  o << std::setw(2) << data << std::endl;
}
