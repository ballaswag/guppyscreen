#include "main_panel.h"
#include "config.h"
#include "state.h"
#include "lvgl/lvgl.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;
#include "icons.h"

#include <algorithm>
#include <string>

LV_FONT_DECLARE(materialdesign_font_40);

static constexpr int FULL_TAB_BAR_W = 60;
static constexpr int COMPACT_TAB_BAR_W = 45;

// Keep the compact screen's nav strip narrow enough to leave more room for the
// page. On larger displays it grows only as far as the established 60px width.
static int tab_bar_w() {
  return std::clamp(scale_w(45), COMPACT_TAB_BAR_W, FULL_TAB_BAR_W);
}

static int reclaimed_tab_bar_w() { return FULL_TAB_BAR_W - tab_bar_w(); }

#define TOOLS_SYMBOL   u8"\U000F1064"
#define HOME_SYMBOL    u8"\U000F02DC"
#define CONSOLE_SYMBOL u8"\U000F018D"
#define SPOOL_SYMBOL   u8"\U000F07DE"

MainPanel::MainPanel(KWebSocketClient &websocket,
		     std::mutex &lock,
		     SpoolmanPanel &sm,
		     MmuPanel *mmu)
  : NotifyConsumer(lock)
  , ws(websocket)
  , homing_panel(ws, lock)
  , fan_panel(ws, lock)
  , led_panel(ws, lock)    
  , tabview(lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, tab_bar_w()))
  , main_tab(lv_tabview_add_tab(tabview, HOME_SYMBOL))
  , mmu_tab(mmu != NULL ? lv_tabview_add_tab(tabview, SPOOL_SYMBOL) : NULL)
  , console_tab(lv_tabview_add_tab(tabview, CONSOLE_SYMBOL))
  , console_panel(ws, lock, console_tab)
  , setting_tab(lv_tabview_add_tab(tabview, TOOLS_SYMBOL))
  , setting_panel(websocket, lock, setting_tab)
  , main_cont(create_screen(main_tab))  // fills the tab: it is the page
  , print_status_panel(websocket, lock, main_cont)
  , print_panel(ws, lock, print_status_panel)
  , numpad(Numpad(main_cont))
  , extruder_panel(ws, lock, numpad, sm)
  , prompt_panel(websocket, lock, main_cont, print_status_panel)
  , spoolman_panel(sm)
  , mmu_panel(mmu)
  , temp_cont(lv_obj_create(main_cont))
  , temp_chart_box(lv_obj_create(main_cont))
  , temp_chart(lv_chart_create(temp_chart_box))
  , homing_btn(main_cont, Icons::MOVE, "Homing", &MainPanel::_handle_homing_cb, this)
  , extrude_btn(main_cont, Icons::FILAMENT_IMG, "Extrude", &MainPanel::_handle_extrude_cb, this)
  , action_btn(main_cont, Icons::FAN, "Fans", &MainPanel::_handle_fanpanel_cb, this)
  , led_btn(main_cont, Icons::LIGHT_IMG, "LED", &MainPanel::_handle_ledpanel_cb, this)
  , print_btn(main_cont, Icons::PRINT, "Print", &MainPanel::_handle_print_cb, this)
  , emergency_btn(main_cont, Icons::EMERGENCY, "Stop", &MainPanel::_handle_emergency_cb, this,
                  "Emergency Stop", "Do you want to emergency stop?",
                  {"Back", "Emergency Stop"})
{
  emergency_btn.set_prompt_condition([] {
    return Config::get_instance()->get<bool>("/ui/prompt_emergency_stop");
  });
    ws.register_notify_update(this);

    lv_obj_add_event_cb(tabview, &MainPanel::_tabview_event_cb,
                            LV_EVENT_VALUE_CHANGED, this);
    set_klipper_ready(false);
}

MainPanel::~MainPanel() {
  if (tabview != NULL) {
    lv_obj_del(tabview);
    tabview = NULL;
  }

  sensors.clear();
}

void MainPanel::subscribe() {
  LOG_TRACE("main panel subscribing");
  print_panel.subscribe();
}

void MainPanel::set_firmware_restart_available(bool available) {
  std::lock_guard<std::mutex> lock(lv_lock);
  setting_panel.set_firmware_restart_available(available);
}

void MainPanel::set_klipper_ready(bool ready) {
  std::lock_guard<std::mutex> lock(lv_lock);
  klipper_ready = ready;
  console_panel.set_gcode_input_enabled(ready);

  for (ButtonContainer *button : {&homing_btn, &extrude_btn, &action_btn,
                                  &led_btn, &print_btn, &emergency_btn}) {
    if (ready) button->enable();
    else button->disable();
  }
  for (const auto &sensor : sensors) sensor.second->set_enabled(ready);
  if (ready) lv_obj_clear_state(print_status_panel.get_mini_status(), LV_STATE_DISABLED);
  else {
    lv_obj_add_state(print_status_panel.get_mini_status(), LV_STATE_DISABLED);
    print_status_panel.background();
    numpad.background();
  }
}

bool MainPanel::home_active() const {
  return lv_tabview_get_tab_act(tabview) == lv_obj_get_index(main_tab);
}

void MainPanel::add_tab_change_cb(lv_event_cb_t cb, void *user_data) {
  lv_obj_add_event_cb(tabview, cb, LV_EVENT_VALUE_CHANGED, user_data);
}

void MainPanel::remove_tab_change_cb(lv_event_cb_t cb, void *user_data) {
  lv_obj_remove_event_cb_with_user_data(tabview, cb, user_data);
}

void MainPanel::init(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/result/status/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/result/status/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }
  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
  if (mmu_panel != NULL) mmu_panel->init_state();
}

void MainPanel::consume(json &j) {  
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/params/0/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/params/0/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    if (klipper_ready && pstat_state.template get<std::string>() != "printing") {
      homing_btn.enable();
      extrude_btn.enable();
    } else {
      homing_btn.disable();
      extrude_btn.disable();
    }
  }

  led_btn.set_image(led_panel.get_main_button_image());
}

static void scroll_begin_event(lv_event_t * e) {
  /*Disable the scroll animations. Triggered when a tab button is clicked */
  if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
    lv_anim_t * a = (lv_anim_t*)lv_event_get_param(e);
    if(a)  a->time = 0;
  }
}

void MainPanel::_tabview_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    auto *self = static_cast<MainPanel*>(lv_event_get_user_data(e));
    lv_obj_t *tv = lv_event_get_target(e);

    const uint16_t idx = lv_tabview_get_tab_act(tv);

    if (idx == lv_obj_get_index(self->setting_tab)) {
        self->setting_panel.foreground();
    }
}

void MainPanel::create_panel() {
  lv_obj_t *tv_content = lv_tabview_get_content(tabview);
  lv_obj_clear_flag(tv_content, LV_OBJ_FLAG_SCROLLABLE);
  // Modern fences the nav bar off from the content with a hairline; classic
  // has never had one. It goes on the content's left edge rather than the
  // button matrix's right, because the tabview draws over the matrix's own
  // border.
  lv_obj_set_style_border_width(tv_content, frame_w(), 0);
  lv_obj_set_style_border_color(tv_content, col(BORDER), 0);
  lv_obj_set_style_border_side(tv_content, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_add_event_cb(lv_tabview_get_content(tabview), scroll_begin_event, LV_EVENT_SCROLL_BEGIN, NULL);
  
  lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab_btns, 0, 0);
  lv_obj_set_style_pad_all(tab_btns, 0, 0);
  // the selection fills its whole slot; a padded, rounded pill read as a
  // floating bubble rather than part of the bar
  lv_obj_set_style_radius(tab_btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_text_color(tab_btns, col(TEXT), LV_PART_ITEMS);
  // the selected tab is the grey slot with the accent-coloured glyph it has
  // always been (LVGL's tabview paints a checked tab's text with the accent)
  // a wash of grey rather than a solid slab, which is the weight it has always
  // had behind the selected glyph
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_20, LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_bg_color(tab_btns, col(SELECTED), LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_text_color(tab_btns, theme_primary(), LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_outline_width(tab_btns, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_side(tab_btns, 0, LV_PART_ITEMS | LV_STATE_CHECKED);
  // on the items, not the bar: the shared key style sets a text font on the
  // items and would otherwise win over an inherited one
  lv_obj_set_style_text_font(tab_btns, &materialdesign_font_40, LV_PART_ITEMS);
  // tab buttons are btnmatrix glyphs, not images, so they cannot wear icon_disabled;
  // grey them with the same colour it recolours disabled icons with
  lv_obj_set_style_text_color(tab_btns, col(DISABLED),
                              LV_PART_ITEMS | LV_STATE_DISABLED);

  // The page is the theme's background everywhere: the screen, the tabview and
  // each tab. Left alone they paint LVGL's own dark card grey, which only
  // happened to match the stock background_colour.
  lv_obj_set_style_bg_color(lv_scr_act(), col(BG), 0);
  lv_obj_set_style_bg_color(tabview, col(BG), 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
  for (lv_obj_t *tab : {main_tab, console_tab, setting_tab, mmu_tab}) {
    if (tab == NULL) continue;
    lv_obj_add_style(tab, &styles().screen, 0);
    // the tab is only a page behind a panel that pads itself; without this
    // the screen style's gap would double up with the panel's own
    lv_obj_set_style_pad_all(tab, 0, 0);
  }

  if (mmu_tab != NULL) {
    // greyed out until klipper confirms the configured backend is there
    lv_btnmatrix_set_btn_ctrl(tab_btns, lv_obj_get_index(mmu_tab), LV_BTNMATRIX_CTRL_DISABLED);
  }

  create_main(main_tab);
}

void MainPanel::handle_homing_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked homing");
    homing_panel.foreground();
  }
}

void MainPanel::handle_extrude_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked extruder");
    extruder_panel.foreground();
  }
}

void MainPanel::handle_fanpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked fan panel");
    fan_panel.foreground();
  }
}

void MainPanel::handle_ledpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked led panel");
    led_panel.activate();
    led_btn.set_image(led_panel.get_main_button_image());
  }
}

void MainPanel::handle_print_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked print");
    print_panel.foreground();
  }
}

void MainPanel::handle_emergency_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked emergency");
    ws.send_jsonrpc("printer.emergency_stop");
  }
}

void MainPanel::create_main(lv_obj_t * parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {0, 0, 0, 0, LV_GRID_TEMPLATE_LAST};

  // Give the temperature side all of the width recovered from the compact
  // nav strip, plus a little more room from the action side. At 480px this is
  // 16:16:13:13 (about 55:45); at 800px it returns to four equal columns.
  const int compact_range = FULL_TAB_BAR_W - COMPACT_TAB_BAR_W;
  const int temp_col_weight = 13 + 3 * reclaimed_tab_bar_w() / compact_range;
  grid_main_col_dsc[0] = LV_GRID_FR(temp_col_weight);
  grid_main_col_dsc[1] = LV_GRID_FR(temp_col_weight);
  grid_main_col_dsc[2] = LV_GRID_FR(13);
  grid_main_col_dsc[3] = LV_GRID_FR(13);

  lv_obj_set_grid_dsc_array(main_cont, grid_main_col_dsc, grid_main_row_dsc);

  // the six actions are tappable tiles, the same look as an MMU slot card
  for (ButtonContainer *b : {&homing_btn, &extrude_btn, &action_btn,
                             &led_btn, &print_btn, &emergency_btn}) {
    b->use_card();
  }

  lv_obj_set_grid_cell(homing_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(action_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(led_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(emergency_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(temp_cont, &styles().row, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(temp_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 3);
  // The chart is the last child of the readout column, half its height; the
  // rows above share the other half, for any number of sensors.
  lv_obj_set_parent(temp_chart_box, temp_cont);
  // while printing, the status chip is the first row of the column (it is
  // hidden otherwise, and flex skips hidden children)
  lv_obj_set_parent(print_status_panel.get_mini_status(), temp_cont);
  lv_obj_move_to_index(print_status_panel.get_mini_status(), 0);

  // the temperature popout covers the tile column, from the column gap out
  lv_obj_update_layout(main_cont);
  numpad.cover_from(lv_obj_get_x(homing_btn.get_container()) - gap());

  // lv_chart places primary-Y tick labels at obj->coords.x1 minus the label
  // width -- outside the widget, not inside its padding (lv_chart.c:1414). So
  // styling the chart itself threw the labels onto whatever sat to its left,
  // which is how they ended up over the nav bar. The box carries the panel
  // look and a left gutter; the chart is transparent inside it, and the labels
  // land in that gutter.
  lv_obj_add_style(temp_chart_box, &styles().panel, 0);
  // the graph keeps the hairline frame it has always been drawn in: it is a
  // plotted box, not a group of controls
  lv_obj_set_style_border_width(temp_chart_box, border_w(), 0);
  lv_obj_set_style_border_color(temp_chart_box, col(BORDER), 0);
  lv_obj_clear_flag(temp_chart_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(temp_chart_box, LV_PCT(100));
  // the chart and the readout rows share the column by flex weight: the chart
  // weighs as much as all the rows together (set in create_sensors), so it is
  // half the column when idle and everything shrinks together when the print
  // status chip joins the column
  lv_obj_set_height(temp_chart_box, 0);
  lv_obj_set_flex_grow(temp_chart_box, 1);
  // room for the tick labels, which lv_chart draws to the left of the plot
  const lv_coord_t tick_gutter = scale_w(30);
  lv_obj_set_style_pad_left(temp_chart_box, tick_gutter, 0);
  lv_obj_set_style_pad_right(temp_chart_box, gap(), 0);
  // the tick labels centre on the 0 and 300 lines, so leave half a line of
  // room above and below the plot for them
  lv_obj_set_style_pad_ver(temp_chart_box, gap() + scale_font(12)->line_height / 2, 0);

  lv_obj_add_style(temp_chart, &styles().row, 0);
  lv_obj_set_size(temp_chart, LV_PCT(100), LV_PCT(100));
  // lv_chart skips the top and bottom guides when it thinks it has a border
  // there (it checks the side, not the width), so the 0 line went undrawn
  lv_obj_set_style_border_side(temp_chart, LV_BORDER_SIDE_NONE, 0);
  lv_obj_set_style_text_font(temp_chart, scale_font(12), LV_PART_TICKS);
  lv_obj_set_style_text_color(temp_chart, col(TEXT_DIM), LV_PART_TICKS);
  lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);
  // hairline guides in the raised grey (the stock border grey, but a theme
  // that hides its borders keeps its graph lines); a 2px trace on top
  lv_obj_set_style_line_width(temp_chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(temp_chart, col(RAISED), LV_PART_MAIN);
  lv_obj_set_style_line_width(temp_chart, 2, LV_PART_ITEMS);

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
  // minor_cnt must stay non-zero: lv_chart derives its tick count as
  // (major_cnt - 1) * minor_cnt and draws nothing at all when that is 0
  lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 5, 1, true, tick_gutter);

  // one guide per labelled value: 0, 75, 150, 225, 300
  lv_chart_set_div_line_count(temp_chart, 5, 0);
  lv_chart_set_point_count(temp_chart, 5000);
  lv_chart_set_zoom_x(temp_chart, 5000);
  // the history scrolls under a finger, but a bar over the trace only clutters
  lv_obj_set_scrollbar_mode(temp_chart, LV_SCROLLBAR_MODE_OFF);
  lv_obj_scroll_to_x(temp_chart, LV_COORD_MAX, LV_ANIM_OFF);
}

void MainPanel::create_sensors(json &temp_sensors) {
  std::lock_guard<std::mutex> lock(lv_lock);
  sensors.clear();
  const int compact_range = FULL_TAB_BAR_W - COMPACT_TAB_BAR_W;
  const int compact_amount = reclaimed_tab_bar_w();
  // Preserve the original balanced chart layout for a short list. Once it
  // grows, fixed-height rows and vertical scrolling keep every tool visible.
  const bool many_sensors = temp_sensors.size() > 4;
  if (many_sensors) {
    lv_obj_add_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(temp_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(temp_cont, LV_SCROLLBAR_MODE_AUTO);
  } else {
    lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  }
  for (auto &sensor : temp_sensors.items()) {
    std::string key = sensor.key();
    bool controllable = sensor.value()["controllable"].template get<bool>();

    lv_color_t color_code = lv_palette_main(LV_PALETTE_ORANGE);
    if (!sensor.value()["color"].is_number()) {
      std::string color = sensor.value()["color"].template get<std::string>();
      if (color == "red") {
	      color_code = lv_palette_main(LV_PALETTE_RED);
      } else if (color == "purple") {
	      color_code = lv_palette_main(LV_PALETTE_PURPLE);
      } else if (color == "blue") {
	      color_code = lv_palette_main(LV_PALETTE_BLUE);
      }
    } else {
      color_code = lv_palette_main((lv_palette_t)sensor.value()["color"].template get<int>());
    }

    std::string display_name = sensor.value()["display_name"].template get<std::string>();

    const void* sensor_img = Icons::HEATER;
    if (key.rfind("extruder", 0) == 0) {
      sensor_img = Icons::EXTRUDER;
    } else if (key == "heater_bed") {
      sensor_img = Icons::BED;
    }

    lv_chart_series_t *temp_series =
      lv_chart_add_series(temp_chart, color_code, LV_CHART_AXIS_PRIMARY_Y);

    auto sc = std::make_shared<SensorContainer>(ws, temp_cont, sensor_img,
			   display_name.c_str(), color_code, controllable, false, numpad, key,
        		   temp_chart, temp_series);
    sc->set_enabled(klipper_ready);
    if (many_sensors) {
      const int compact_trim = 2 * compact_amount / compact_range;
      lv_obj_set_height(sc->get_sensor(), scale_r(34) - compact_trim);
    } else {
      lv_obj_set_height(sc->get_sensor(), 0);
      lv_obj_set_flex_grow(sc->get_sensor(), 1);
      lv_obj_set_style_min_height(sc->get_sensor(), scale_r(30), 0);
    }
    sensors.insert({key, sc});
  }

  // the readouts were appended after the chart, so put the chart back at the
  // bottom of the column where it belongs
  const uint32_t last = lv_obj_get_child_cnt(temp_cont);
  if (last > 0) lv_obj_move_to_index(temp_chart_box, last - 1);
  lv_obj_set_height(temp_chart_box, many_sensors ? scale_r(110) : 0);
  const int sensor_weight = std::max<int>(1, sensors.size());
  const int compact_chart_weight = compact_amount / compact_range;
  lv_obj_set_flex_grow(temp_chart_box,
                       many_sensors ? 0 : sensor_weight + compact_chart_weight);
}

void MainPanel::create_fans(json &fans) {
  fan_panel.create_fans(fans);
}

void MainPanel::create_leds(json &leds) {
  std::lock_guard<std::mutex> lock(lv_lock);
  // runs again on every klipper reconnect, so a hidden button has to come back.
  // Without leds print takes the led spot and stop spans the bottom row.
  const bool has_leds = leds.is_array() && !leds.empty();
  if (has_leds) {
    led_btn.show();
    lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
    lv_obj_set_grid_cell(emergency_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  } else {
    led_btn.hide();
    lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_grid_cell(emergency_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 2, 1);
  }
  led_panel.init(leds);
  led_btn.set_image(led_panel.get_main_button_image());
}

void MainPanel::enable_spoolman() {
  spoolman_panel.init();
  extruder_panel.enable_spoolman();
}

void MainPanel::enable_mmu() {
  if (mmu_tab == NULL || mmu_panel == NULL) return;

  LOG_DEBUG("enabling mmu panel");
  std::lock_guard<std::mutex> lock(lv_lock);
  mmu_panel->create(mmu_tab);
  lv_btnmatrix_clear_btn_ctrl(lv_tabview_get_tab_btns(tabview),
                              lv_obj_get_index(mmu_tab), LV_BTNMATRIX_CTRL_DISABLED);
}

// Klipper came back without the backend this time. The tab may already be
// built and showing the previous session's slots, so empty it and take the
// user off it before greying the button again.
void MainPanel::disable_mmu() {
  if (mmu_tab == NULL || mmu_panel == NULL) return;

  LOG_DEBUG("disabling mmu panel");
  std::lock_guard<std::mutex> lock(lv_lock);
  mmu_panel->clear();
  const uint16_t idx = lv_obj_get_index(mmu_tab);
  if (lv_tabview_get_tab_act(tabview) == idx) {
    lv_tabview_set_act(tabview, lv_obj_get_index(main_tab), LV_ANIM_OFF);
  }
  lv_btnmatrix_set_btn_ctrl(lv_tabview_get_tab_btns(tabview), idx, LV_BTNMATRIX_CTRL_DISABLED);
}
