#include "hv/requests.h"
#include "hv/hurl.h"
#include "config.h"
#include "state.h"
#include "logger.h"
#include "platform.h"

#include <cmath>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <experimental/filesystem>
#include <regex>
#include <functional>

namespace fs = std::experimental::filesystem;

namespace KUtils {
  std::string get_root_path(const std::string root_name) {
    auto roots = State::get_instance()->get_data("/roots"_json_pointer);
    json filtered;
    std::copy_if(roots.begin(), roots.end(),
		 std::back_inserter(filtered), [&root_name](const json& item) {
		   return item.contains("name") && item["name"] == root_name;
		 });

    if (get_log_level() <= LogLevel::TRACE) {
      LOG_TRACE("roots {}, filtered {}", roots.dump(), filtered.dump());
    }
    if (!filtered.empty()) {
      return filtered["/0/path"_json_pointer];
    }

    return "";
  }

  std::pair<std::string, size_t> get_thumbnail(const std::string &gcode_file, json &j, int target_width) {
    auto &thumbs = j["/result/thumbnails"_json_pointer];
    if (!thumbs.is_null() && !thumbs.empty()) {
      // assume square, look for the one closest to what the caller asked for
      const int scaled_width = target_width;
      LOG_DEBUG("using thumb at scaled width {}", scaled_width);
      uint32_t closest_index = 0;
      size_t thumb_width = 0;
      auto width = thumbs.at(0)["width"].is_number()
	        ? thumbs.at(0)["width"].template get<int>()
	        : std::stoi(thumbs.at(0)["width"].template get<std::string>());
      int closest = std::abs(scaled_width - width);
      for (int i = 0; i < thumbs.size(); i++) {
	      width = thumbs.at(i)["width"].is_number()
	        ? thumbs.at(i)["width"].template get<int>()
	        : std::stoi(thumbs.at(i)["width"].template get<std::string>());
	      int cur_diff = std::abs(scaled_width - width);
        if (cur_diff < closest) {
          closest = cur_diff;
          closest_index = i;
          thumb_width = width;
        }
      }

      auto &thumb = thumbs.at(closest_index);
      if (get_log_level() <= LogLevel::DEBUG) {
        LOG_DEBUG("using thumb at index {}, {}", closest_index, thumbs.dump());
      }

      // metadata thumbnail paths are relative to the current gcode file directory
      std::string relative_path = thumb["relative_path"].template get<std::string>();
      size_t found = gcode_file.find_last_of("/\\");
      if (found != std::string::npos) {
	      relative_path = gcode_file.substr(0, found + 1) + relative_path;
      }

      Config *conf = Config::get_instance();
      std::string moonraker_host = conf->get<std::string>("/moonraker/host");
      std::string fname = relative_path.substr(relative_path.find_last_of("/\\") + 1);

      const bool is_running_local = moonraker_host == "localhost" || moonraker_host == "127.0.0.1";
      std::string thumbnail_path = conf->get<std::string>("/moonraker/thumbnail_path");

      // if not running locally a thumbnail path is required
      if (!is_running_local && thumbnail_path == "") {
        LOG_ERROR("Thumbnail path is not defined");
        return std::make_pair("", 0);
      }

      std::string fullpath;
      if (is_running_local && thumbnail_path == "") {
        auto gcode_root = get_root_path("gcodes");
        fullpath = fmt::format("{}/{}", gcode_root, relative_path);
      } else { // download thumbnail
        if (fs::exists(fs::path(thumbnail_path))) {
          // Different folders can contain identically named G-code previews.
          fullpath = fmt::format("{}/{:x}{}", thumbnail_path, std::hash<std::string>{}(relative_path),
              fs::path(fname).extension().string());
          std::string thumb_url = fmt::format("http://{}:{}/server/files/gcodes/{}",
                      moonraker_host,
                      conf->get<uint32_t>("/moonraker/port"),
                      HUrl::escape(relative_path, "/"));

          LOG_DEBUG("Download thumb {} -> {}", thumb_url, fullpath);
          auto size = requests::downloadFile(thumb_url.c_str(), fullpath.c_str());
          LOG_TRACE("downloaded size {}", size);
        } else {
          LOG_ERROR("Thumbnail path {} does not exist", thumbnail_path);
          return std::make_pair("", 0);
        }
      }
      return std::make_pair(fullpath, thumb_width);
    }
    return std::make_pair("", 0);
  }

  std::string get_obj_name(const std::string &id) {
    size_t pos = id.find_last_of(' ');
    return id.substr(pos + 1);
  }

  std::string to_title(std::string s) {
    bool last = true;
    for (char& c : s) {
      c = last ? std::toupper(c) : std::tolower(c);
      if (c == '_') {
	      c = ' ';
      }
      last = std::isspace(c);
    }
    return s;
  }

  std::string eta_string(int64_t s) {
    time_t seconds (s);
    tm p;
    gmtime_r (&seconds, &p);

    std::ostringstream os;

    if (p.tm_yday > 0) {
      os << p.tm_yday << "d ";
    }

    if (p.tm_hour > 0) {
      os << p.tm_hour << "h ";
    }

    if (p.tm_min > 0) {
      os << p.tm_min << "m ";
    }

    os << p.tm_sec << "s";
    
    return os.str();
  }

  size_t bytes_to_mb(size_t s) {
    return s / 1024 / 1024;
  }
}  // namespace KUtils
