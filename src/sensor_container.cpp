#include "sensor_container.h"
#include "logger.h"
#include "state.h"
#include "theme.h"
#include "utils.h"
#include <cctype>
#include <string>

using namespace Theme;

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
  , accent(modern() ? lv_obj_create(sensor_cont) : NULL)
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
    // A row card laid out with flex instead of seven hand-tuned offsets from
    // the right edge: the name takes the slack, so the row fills whatever
    // width it is given at any resolution and nothing collides.
    lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(sensor_cont, &styles().row_card, 0);
    lv_obj_set_width(sensor_cont, LV_PCT(100));
    // one line tall unless the panel says otherwise (the home column stretches
    // its rows to share the space above the chart)
    lv_obj_set_height(sensor_cont, scale_r(34));
    lv_obj_set_flex_flow(sensor_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sensor_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sensor_cont, gap(), 0);
    // half the card gap top and bottom: a 30px row has no room for a full one
    const lv_coord_t pad_v = gap() / 2;
    lv_obj_set_style_pad_ver(sensor_cont, pad_v, 0);

    // The sensor's colour goes down the row's left edge either way; what
    // differs is whether it is the row's border or a bar of its own.
    //
    // An object has exactly one border. Classic spends it on the stripe, which
    // is why the row has no frame in that look. Modern wants the frame, so the
    // stripe becomes a floating child instead: placed at the content's left
    // minus the padding, stretched by the vertical padding to span the whole
    // card, and clipped to the card's corners.
    if (modern()) {
      const lv_coord_t bar = gap() * 2 / 3;
      lv_obj_set_style_pad_left(sensor_cont, 2 * bar, 0);
      lv_obj_set_style_clip_corner(sensor_cont, true, 0);
      lv_obj_add_style(accent, &styles().row, 0);
      lv_obj_add_flag(accent, LV_OBJ_FLAG_FLOATING);
      lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(accent, bar, LV_PCT(100));
      lv_obj_set_pos(accent, -2 * bar, 0);
      lv_obj_set_style_transform_height(accent, 2 * pad_v, 0);
      lv_obj_set_style_bg_color(accent, color, 0);
      lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    } else {
      lv_obj_set_style_border_side(sensor_cont, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_width(sensor_cont, scale_r(5), 0);
      lv_obj_set_style_border_color(sensor_cont, color, 0);
      lv_obj_set_style_pad_left(sensor_cont, gap(), 0);
    }

    // The icon is drawn at a shade under three fifths of the bitmap, the size
    // these rows have always shown, and it stays there: fitting it to the row
    // instead leaves it at whatever height the row happened to have when it was
    // first laid out, which is the minimum, not the height it ends up with.
    lv_img_set_src(sensor_img, img);
    fit_img(sensor_img, 0, 0, 150);

    lv_label_set_text(sensor_label, text);
    lv_obj_set_flex_grow(sensor_label, 1);
    lv_label_set_long_mode(sensor_label, LV_LABEL_LONG_DOT);  // the values keep their room

    lv_label_set_text(value_label, "0");
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_label_set_text(divider_label, "/");

    if (show_target || can_edit) {
      lv_label_set_text(target_label, "0");
      lv_obj_set_style_text_align(target_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
      lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (can_edit) {
      // The editable target is outlined rather than filled, the way it always
      // has been: a thin box round the number says "you can change this"
      // without turning a temperature readout into a button.
      lv_obj_set_style_radius(target_label, radius_md(), 0);
      lv_obj_set_style_border_width(target_label, scale_r(2), 0);
      lv_obj_set_style_border_color(target_label, col(DISABLED), 0);
      // accent while the numpad is open for this row
      lv_obj_set_style_border_color(target_label, theme_primary(), LV_STATE_CHECKED);
      lv_obj_set_style_pad_hor(target_label, gap(), 0);
      lv_obj_set_style_pad_ver(target_label, gap() / 3, 0);
      lv_obj_set_style_min_width(target_label, scale_w(36), 0);

      LOG_DEBUG("sensor cb registered name {}", id);
      lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit, LV_EVENT_CLICKED, this);
      lv_obj_add_style(sensor_cont, &styles().card_pressed, LV_STATE_PRESSED);
    } 
}

SensorContainer::~SensorContainer() {
  if (sensor_cont != NULL) {
    LOG_DEBUG("deleting sensor {}", id);
    numpad.release_if_target(target_label);
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

void SensorContainer::set_enabled(bool enabled) {
  if (enabled) lv_obj_clear_state(sensor_cont, LV_STATE_DISABLED);
  else lv_obj_add_state(sensor_cont, LV_STATE_DISABLED);
}

void SensorContainer::update_target(int new_target) {
  if (new_target >= 0 && new_target != target) {
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
      // The first reading fills the whole history. lv_chart's dense-points
      // mode seeds its running min/max from the point before the first real
      // one, and an unset point there drew every trace as a spike from the top.
      if (!series_seeded) {
        lv_chart_set_all_value(chart, series, v);
        series_seeded = true;
      }
      lv_chart_set_next_value(chart, series, v);
      last_updated_ts = std::time(nullptr);
    }
  }
}

void SensorContainer::handle_edit(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    LOG_TRACE("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    // klipper refuses a target outside the configured range
    auto &settings = State::get_instance()->get_data("/printer_state/configfile/settings"_json_pointer);
    double min_temp = std::numeric_limits<double>::lowest();
    double max_temp = std::numeric_limits<double>::max();
    // configfile settings key sections in lower case, the object name keeps its case
    std::string section;
    for (char c : id) section.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (settings.contains(section)) {
      min_temp = settings[section].value("min_temp", min_temp);
      max_temp = settings[section].value("max_temp", max_temp);
    }
    numpad.set_callback([this](double v) {
      std::string heater_name = KUtils::get_obj_name(id);
      if (id.find("temperature_fan") != std::string::npos) {
        ws.gcode_script(fmt::format("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN={} TARGET={}", heater_name, v));
      } else {
        ws.gcode_script(fmt::format("SET_HEATER_TEMPERATURE HEATER={} TARGET={}", heater_name, v));
      }
    }, min_temp, max_temp);
    numpad.set_highlight_target(target_label);
    numpad.foreground_reset();
  }
}
