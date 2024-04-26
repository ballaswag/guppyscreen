#include "sensor_container.h"
#include "spdlog/spdlog.h"

#include <string>

SensorContainer::SensorContainer(KWebSocketClient &c,
				 lv_obj_t *parent,
				 const void *img,
				 const char *text,
				 lv_color_t color,
				 bool can_edit,
				 bool show_target,
				 Numpad &np,
				 std::string name,
				 lv_obj_t *chart_chart,
				 lv_chart_series_t *chart_series)
  : ws(c)
  , sensor_cont(lv_obj_create(parent))
  , sensor_img(lv_img_create(sensor_cont))
  , sensor_label(lv_label_create(sensor_cont))
  , value_label(lv_label_create(sensor_cont))
  , value(0)
  , divider_label(lv_label_create(sensor_cont))
  , target_label(lv_label_create(sensor_cont))
  , target(-1)
  , numpad(np)
  , id(name)
  , chart(chart_chart)
  , series(chart_series)
  , last_updated_ts(std::time(nullptr))
{
    lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(sensor_cont, color, LV_PART_MAIN);
    lv_obj_set_style_border_side(sensor_cont, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
    lv_obj_set_style_border_width(sensor_cont, 5, LV_PART_MAIN);

    // auto cont_width = (double)lv_disp_get_physical_hor_res(NULL) * 0.4125;
    // cont_width = cont_width > 330 ? 330 : cont_width;
    // auto cont_height = (double)lv_disp_get_physical_ver_res(NULL) * 0.125;
    // cont_height = cont_height > 60 ? 60 : cont_height;

    auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
    auto height_scale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
    lv_obj_set_size(sensor_cont, 330 * width_scale, 60 * height_scale);
    lv_obj_set_style_pad_all(sensor_cont, 0, 0);

    lv_img_set_src(sensor_img, img);
    lv_obj_align(sensor_img, LV_ALIGN_LEFT_MID, 0, 0);

    lv_label_set_text(sensor_label, text);
    lv_obj_align_to(sensor_label, sensor_img, LV_ALIGN_OUT_RIGHT_MID, -7 * width_scale, 0);

    lv_label_set_text(value_label, "0");
    lv_obj_set_width(value_label, 50 * width_scale);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -75 * width_scale, 0);
    lv_obj_set_style_pad_all(value_label, 8 * width_scale, 0);

    lv_label_set_text(divider_label, "/");
    lv_obj_set_width(divider_label, 50 * width_scale);
    lv_obj_align(divider_label, LV_ALIGN_RIGHT_MID, -32 * width_scale, 0);
    lv_obj_set_style_pad_all(divider_label, 8 * width_scale, 0);

    if (show_target || can_edit) {
      lv_label_set_text(target_label, "0");
      lv_obj_set_width(target_label, 60 * width_scale);
      lv_obj_align(target_label, LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_set_style_pad_all(target_label, 8 * width_scale, 0);
    } else {
      lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (can_edit) {      
      lv_obj_set_style_border_width(target_label, 2, LV_PART_MAIN);
      lv_obj_set_style_radius(target_label, 6, LV_PART_MAIN);
      lv_obj_set_style_border_color(target_label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);

      spdlog::debug("sensor cb registered name {}, cont {}, this {}, np {}",
		    id, fmt::ptr(sensor_cont), fmt::ptr(this), fmt::ptr(&np));
      lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit, LV_EVENT_CLICKED, this);
    } 
}

SensorContainer::SensorContainer(KWebSocketClient &c,
				 lv_obj_t *parent,
				 const void *img,
				 uint16_t img_scale,
				 const char *text,
				 lv_color_t color,
				 bool can_edit,
				 bool show_target,
				 Numpad &np,
				 std::string name,
				 lv_obj_t *chart,
				 lv_chart_series_t *chart_series)
  : SensorContainer(c, parent, img, text, color, can_edit, show_target, np, name, chart, chart_series)
{
  lv_img_set_zoom(sensor_img, img_scale);
}

SensorContainer::~SensorContainer() {
  if (sensor_cont != NULL) {
    spdlog::debug("deleting sensor {}", id);
    lv_obj_del(sensor_cont);
    sensor_cont = NULL;
  }

  if (series != NULL && chart != NULL) {
    lv_chart_remove_series(chart, series);
    series = NULL;
  }
}

lv_obj_t *SensorContainer::get_sensor() {
  return sensor_cont;
}

void SensorContainer::update_target(int new_target) {
  if (new_target >= 0) {
    target = new_target;
    lv_label_set_text(target_label, fmt::format("{}", new_target).c_str());
  }
}

void SensorContainer::update_value(int new_value) {
  if (value != new_value) {
    value = new_value;
    lv_label_set_text(value_label, fmt::format("{}", new_value).c_str());
  }
}

void SensorContainer::update_series(int v) {
  if (series != NULL && chart != NULL) {
    auto delta = std::time(nullptr) - last_updated_ts;
    if (delta > 1) {
      lv_chart_set_next_value(chart, series, v);
      last_updated_ts = std::time(nullptr);
    }
  }
}

void SensorContainer::handle_edit(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    spdlog::trace("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    numpad.set_callback([this](double v) {
      ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER={} TARGET={}", id, v));
    });
    numpad.foreground_reset();
  }
}
