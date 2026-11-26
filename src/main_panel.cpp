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

static constexpr int FULL_TAB_BAR_W = 80;
static constexpr int COMPACT_TAB_BAR_W = 50;

// Square buttons on the normal screen; the compact strip is a little wider
// than its rows so six touch targets still fit in 272px of height.
static int tab_bar_w() {
#ifdef OS_ANDROID
  // Keep scaling past 800x480 on dense phone and tablet displays.
  return std::clamp(scale_w(50), COMPACT_TAB_BAR_W, std::max(FULL_TAB_BAR_W, scale_r(48)));
#else
  return std::clamp(scale_w(50), COMPACT_TAB_BAR_W, FULL_TAB_BAR_W);
#endif
}

static int reclaimed_tab_bar_w() { return std::max(0, FULL_TAB_BAR_W - tab_bar_w()); }

// Stop uses the shared action icon sizing, so the drawn tab icons share its cap.
static int nav_icon_px() { return std::min(action_icon_size(), std::max(40, tab_bar_w() * 2 / 3)); }

MainPanel::MainPanel(KWebSocketClient &websocket,
		     std::mutex &lock,
		     SpoolmanPanel *sm,
		     MmuPanel *mmu)
  : NotifyConsumer(lock)
  , ws(websocket)
  , homing_panel(ws, lock)
  , fan_panel(ws, lock)
  , led_panel(ws, lock)    
  , tabview(lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, tab_bar_w()))
  , main_tab(lv_tabview_add_tab(tabview, " "))
  , filament_tab(mmu != NULL || (sm != NULL && !SpoolmanPanel::hidden())
                     ? lv_tabview_add_tab(tabview, " ") : NULL)
  , print_tab(lv_tabview_add_tab(tabview, " "))
  , console_tab(lv_tabview_add_tab(tabview, " "))
  , console_panel(ws, lock, console_tab)
  , setting_tab(lv_tabview_add_tab(tabview, " "))
  , setting_panel(websocket, lock, setting_tab)
  , main_cont(create_screen(main_tab))  // fills the tab: it is the page
  , print_status_panel(websocket, lock, main_cont)
  , print_panel(ws, lock, print_status_panel, mmu, print_tab)
  , numpad(Numpad(main_cont))
  , extruder_panel(ws, lock, numpad)
  , prompt_panel(websocket, lock, main_cont, print_status_panel)
  , spoolman_panel(sm)
  , mmu_panel(mmu)
  , temp_cont(lv_obj_create(main_cont))
  , actions_cont(create_row(main_cont))
  , temp_chart_box(lv_obj_create(main_cont))
  , temp_chart(lv_chart_create(temp_chart_box))
  , homing_btn(actions_cont, Icons::MOVE, "Homing", &MainPanel::_handle_homing_cb, this)
  , extrude_btn(actions_cont, Icons::EXTRUDE_IMG, "Extruder", &MainPanel::_handle_extrude_cb, this)
  , action_btn(actions_cont, Icons::FAN, "Fans", &MainPanel::_handle_fanpanel_cb, this)
  , led_btn(actions_cont, Icons::LIGHT_IMG, "LED", &MainPanel::_handle_ledpanel_cb, this)
  , emergency_btn(tabview, Icons::EMERGENCY, "Stop", &MainPanel::_handle_emergency_cb, this,
                  "Emergency Stop", "Do you want to emergency stop?",
                  {"Back", "Emergency Stop"})
{
  print_panel.print_started = [this] { show_home(); };
  emergency_btn.set_prompt_condition([] {
    return Config::get_instance()->get<bool>("/ui/prompt_emergency_stop");
  });
    if (spoolman_panel != NULL && !SpoolmanPanel::hidden()) {
      spoolman_panel->attach(filament_tab);
    }
    ws.register_notify_update(this);
    if (mmu_panel != NULL && spoolman_panel != NULL) {
      mmu_panel->open_spoolman = [this]() { spoolman_panel->foreground(); };
      mmu_panel->pick_spool = [this](const std::string &lane, int current, std::function<void(int)> done) {
        spoolman_panel->pick(lane, current, std::move(done));
      };
      spoolman_panel->lanes_of = [this](int id) { return mmu_panel->spool_lanes(id); };
    }

    lv_obj_add_event_cb(tabview, &MainPanel::_tabview_event_cb,
                            LV_EVENT_VALUE_CHANGED, this);
    set_klipper_ready(false);
}

MainPanel::~MainPanel() {
  print_panel.background();
  if (tabview != NULL) {
    if (spoolman_panel != NULL) {
      spoolman_panel->detach();
    }
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
  if (!ready) {
    disable_mmu();
  }
  std::lock_guard<std::mutex> lock(lv_lock);
  klipper_ready = ready;
  console_panel.set_gcode_input_enabled(ready);

  for (ButtonContainer *button : {&homing_btn, &extrude_btn, &action_btn,
                                  &led_btn, &emergency_btn}) {
    if (ready) {
      button->enable();
    }
    else {
      button->disable();
    }
  }
  update_print_tab();
  for (const auto &sensor : sensors) {
    sensor.second->set_enabled(ready);
  }
  if (ready) {
    lv_obj_clear_state(print_status_panel.get_mini_status(), LV_STATE_DISABLED);
  }
  else {
    lv_obj_add_state(print_status_panel.get_mini_status(), LV_STATE_DISABLED);
    print_status_panel.background();
    homing_panel.background();
    extruder_panel.background();
    fan_panel.background();
    led_panel.background();
    print_panel.background();
    numpad.background();
  }
}

void MainPanel::update_print_tab() {
  auto *buttons = lv_tabview_get_tab_btns(tabview);
  const auto index = lv_obj_get_index(print_tab);
  if (klipper_ready && !PrintPanel::printer_busy()) {
    lv_btnmatrix_clear_btn_ctrl(buttons, index, LV_BTNMATRIX_CTRL_DISABLED);
  } else {
    lv_btnmatrix_set_btn_ctrl(buttons, index, LV_BTNMATRIX_CTRL_DISABLED);
    if (lv_tabview_get_tab_act(tabview) == index) {
      show_home();
    }
  }
}

void MainPanel::show_home() {
  lv_tabview_set_act(tabview, lv_obj_get_index(main_tab), LV_ANIM_OFF);
  lv_event_send(tabview, LV_EVENT_VALUE_CHANGED, NULL);
}

void MainPanel::append_console_output(const std::string &text) {
  std::lock_guard<std::mutex> lock(lv_lock);
  console_panel.append_output(text);
}

bool MainPanel::home_active() const {
  return lv_tabview_get_tab_act(tabview) == lv_obj_get_index(main_tab);
}

#ifdef OS_ANDROID
bool MainPanel::printer_menu_visible() {
  const int main_index = lv_obj_get_index(tabview);
  // Page panels leave the strip exposed. Only a full-screen panel above the
  // tabview (including print tuning and macro prompts) covers the menu.
  for (uint32_t i = main_index + 1; i < lv_obj_get_child_cnt(lv_scr_act()); i++) {
    auto *panel = lv_obj_get_child(lv_scr_act(), i);
    if (panel != prompt_panel.get_container() && lv_obj_get_style_bg_opa(panel, 0) != LV_OPA_COVER) {
      continue;
    }
    if (lv_obj_is_visible(panel) && lv_obj_get_x(panel) <= 0 && lv_obj_get_y(panel) <= 0 &&
        lv_obj_get_width(panel) >= lv_obj_get_width(tabview) &&
        lv_obj_get_height(panel) >= lv_obj_get_height(tabview)) {
      return false;
    }
  }
  return true;
}
#endif

void MainPanel::add_tab_change_cb(lv_event_cb_t cb, void *user_data) {
  lv_obj_add_event_cb(tabview, cb, LV_EVENT_VALUE_CHANGED, user_data);
}

void MainPanel::remove_tab_change_cb(lv_event_cb_t cb, void *user_data) {
  lv_obj_remove_event_cb_with_user_data(tabview, cb, user_data);
}

// The graph tops out just above the hottest reading in its history. A multiple
// of 20 keeps the four tick steps whole numbers.
void MainPanel::fit_chart_range() {
  auto now = std::time(nullptr);
  if (now - chart_range_ts < 2) {
    return;
  }
  chart_range_ts = now;

  lv_coord_t highest = 0;
  const uint16_t points = lv_chart_get_point_count(temp_chart);
  lv_chart_series_t *series = lv_chart_get_series_next(temp_chart, NULL);
  for (; series != NULL; series = lv_chart_get_series_next(temp_chart, series)) {
    const lv_coord_t *values = lv_chart_get_y_array(temp_chart, series);
    for (uint16_t i = 0; i < points; i++) {
      if (values[i] != LV_CHART_POINT_NONE && values[i] > highest) {
        highest = values[i];
      }
    }
  }

  const int top = (highest / 20 + 1) * 20;
  if (top != chart_top) {
    chart_top = top;
    lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  }
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
  fit_chart_range();
  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
  update_print_tab();
  if (mmu_panel != NULL) {
    mmu_panel->init_state();
  }
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
  fit_chart_range();

  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (!pstat_state.is_null()) {
    update_print_tab();
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
    if(a) {
      a->time = 0;
    }
  }
}

const lv_img_dsc_t *MainPanel::tab_icon(uint16_t id) const {
  auto is = [id](lv_obj_t *tab) { return tab != NULL && lv_obj_get_index(tab) == id; };
  if (is(main_tab)) {
    return Icons::NAV_HOME;
  }
  if (is(print_tab)) {
    return Icons::PRINT;
  }
  if (is(filament_tab)) {
    return mmu_available || SpoolmanPanel::hidden() ? Icons::NAV_SPOOL : Icons::NAV_SPOOLMAN;
  }
  if (is(console_tab)) {
    return Icons::NAV_CONSOLE;
  }
  if (is(setting_tab)) {
    return Icons::NAV_TOOLS;
  }
  return NULL;
}

void MainPanel::_draw_tab_icon(lv_event_t *e) {
  auto *self = static_cast<MainPanel*>(lv_event_get_user_data(e));
  auto *part = static_cast<lv_obj_draw_part_dsc_t*>(lv_event_get_param(e));
  if (part->part != LV_PART_ITEMS) {
    return;
  }
  const auto *icon = self->tab_icon(part->id);
  if (icon == NULL) {
    return;
  }
  // LVGL changes draw_area to the label bounds before DRAW_PART_END.
  // Each button's BEGIN/END pair finishes before the next button is drawn.
  if (lv_event_get_code(e) == LV_EVENT_DRAW_PART_BEGIN) {
    self->tab_icon_area = *part->draw_area;
    // The image replaces the tab's placeholder label.
    part->label_dsc->opa = LV_OPA_TRANSP;
    return;
  }

  const auto &slot = self->tab_icon_area;
  // Ask the decoder so this also works with encoded image sources.
  lv_img_header_t header;
  if (lv_img_decoder_get_info(icon, &header) != LV_RES_OK || header.w == 0 || header.h == 0) {
    return;
  }
  const int icon_px = lv_obj_get_content_width(self->emergency_btn.get_container());
  const int size = std::max(1, std::min({icon_px, lv_area_get_width(&slot) - 2 * gap(),
                                      lv_area_get_height(&slot)}));
  const int zoom = size * LV_IMG_ZOOM_NONE / std::max(header.w, header.h);
  const int width = header.w * zoom / LV_IMG_ZOOM_NONE;
  const int height = header.h * zoom / LV_IMG_ZOOM_NONE;
  lv_area_t area;
  area.x1 = slot.x1 + (lv_area_get_width(&slot) - width) / 2;
  area.y1 = slot.y1 + (lv_area_get_height(&slot) - height) / 2;
  area.x2 = area.x1 + header.w - 1;
  area.y2 = area.y1 + header.h - 1;

  lv_draw_img_dsc_t image;
  lv_draw_img_dsc_init(&image);
  image.zoom = zoom;
  image.pivot = {0, 0};
  image.recolor_opa = LV_OPA_COVER;
  const bool disabled = lv_btnmatrix_has_btn_ctrl(lv_event_get_target(e), part->id,
                                                 LV_BTNMATRIX_CTRL_DISABLED);
  image.recolor = disabled ? col(DISABLED)
                          : lv_tabview_get_tab_act(self->tabview) == part->id ? theme_primary() : col(TEXT);
  lv_draw_img(part->draw_ctx, &image, &area, icon);
}

void MainPanel::_tabview_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
      return;
    }

    auto *self = static_cast<MainPanel*>(lv_event_get_user_data(e));
    // Navigation button events bubble up with the button matrix as their
    // target. Read the tabview receiving the event, and ignore page controls.
    lv_obj_t *tv = lv_event_get_current_target(e);
    auto *target = lv_event_get_target(e);
    if (target != tv && target != lv_tabview_get_tab_btns(tv)) {
      return;
    }

    const uint16_t idx = lv_tabview_get_tab_act(tv);

    // Button-matrix events also arrive when Home is already selected, so it
    // returns from a home action panel as well as switching from another tab.
    self->homing_panel.background();
    self->extruder_panel.background();
    self->fan_panel.background();
    self->led_panel.background();
    self->print_status_panel.background();
    self->numpad.background();

    if (idx == lv_obj_get_index(self->print_tab)) {
        if (!self->klipper_ready || PrintPanel::printer_busy()) {
            self->update_print_tab();
            return;
        }
        self->print_panel.foreground();
        return;
    }
    self->print_panel.background();
    if (self->filament_tab != NULL && idx == lv_obj_get_index(self->filament_tab) &&
        !self->mmu_available && self->spoolman_available && !SpoolmanPanel::hidden()) {
        self->spoolman_panel->foreground();
    } else if (idx == lv_obj_get_index(self->console_tab)) {
        self->console_panel.select_first_tab();
    } else if (idx == lv_obj_get_index(self->setting_tab)) {
        self->setting_panel.select_first_tab();
        self->setting_panel.foreground();
    }
}

void MainPanel::create_panel() {
  lv_obj_t *tv_content = lv_tabview_get_content(tabview);
  lv_obj_clear_flag(tv_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(lv_tabview_get_content(tabview), scroll_begin_event, LV_EVENT_SCROLL_BEGIN, NULL);
  
  lv_obj_t * tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tab_btns, 0, 0);
  lv_obj_set_style_pad_all(tab_btns, 0, 0);
  emergency_btn.use_plain();
  emergency_btn.hide_label();
  auto *stop = emergency_btn.get_container();
  lv_obj_add_flag(stop, LV_OBJ_FLAG_FLOATING);
  // the selection fills its whole slot; a padded, rounded pill read as a
  // floating bubble rather than part of the bar
  lv_obj_set_style_radius(tab_btns, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
  // The selected tab keeps its grey wash; the draw callback colours the icon.
  lv_obj_set_style_bg_opa(tab_btns, LV_OPA_20, LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_bg_color(tab_btns, col(SELECTED), LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_outline_width(tab_btns, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_side(tab_btns, 0, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(tab_btns, &MainPanel::_draw_tab_icon, LV_EVENT_DRAW_PART_BEGIN, this);
  lv_obj_add_event_cb(tab_btns, &MainPanel::_draw_tab_icon, LV_EVENT_DRAW_PART_END, this);
#ifdef OS_ANDROID
  // the app draws the icons from their SVGs, at exactly the size shown
  android_set_nav_icon_px(nav_icon_px());
  // the top of the strip is the app's printer menu, which it draws itself
  lv_obj_set_style_pad_top(tab_btns, android_menu_height(), 0);
  android_place_menu(tab_bar_w(), android_menu_height());
#endif

  // Give tabs and Stop the same square target, limited by the height available
  // on compact screens. Distribute any spare height between and around them.
  const int menu_h = lv_obj_get_style_pad_top(tab_btns, 0);
  const int rows = lv_obj_get_child_cnt(tv_content) + 1;
  const int available_h = lv_disp_get_ver_res(NULL) - menu_h;
  const int row_h = std::max(1, std::min(tab_bar_w(), available_h / rows));
  const int icon_px = std::max(1, std::min({nav_icon_px(), tab_bar_w() - 2 * gap(),
                                         row_h - 2 * gap()}));
  const int spare_h = std::max(0, available_h - rows * row_h);
  const int row_gap = spare_h / (rows + 1);
  const int remainder = spare_h - row_gap * (rows + 1);
  const int top = row_gap + remainder / 2;
  const int bottom = row_gap + remainder - remainder / 2;
  lv_obj_set_style_pad_row(tab_btns, row_gap, 0);
  lv_obj_set_style_pad_top(tab_btns, menu_h + top, 0);
  lv_obj_set_style_pad_bottom(tab_btns, row_h + row_gap + bottom, 0);
  lv_obj_set_style_pad_left(stop, (tab_bar_w() - icon_px) / 2, 0);
  lv_obj_set_style_pad_right(stop, tab_bar_w() - icon_px - (tab_bar_w() - icon_px) / 2, 0);
  lv_obj_set_size(stop, tab_bar_w(), row_h);
  lv_obj_align(stop, LV_ALIGN_BOTTOM_LEFT, 0, -bottom);

  // The page is the theme's background everywhere: the screen, the tabview and
  // each tab. Left alone they paint LVGL's own dark card grey, which only
  // happened to match the stock background_colour.
  lv_obj_set_style_bg_color(lv_scr_act(), col(BG), 0);
  lv_obj_set_style_bg_color(tabview, col(BG), 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);
  for (lv_obj_t *tab : {main_tab, print_tab, console_tab, setting_tab, filament_tab}) {
    if (tab == NULL) {
      continue;
    }
    lv_obj_add_style(tab, &styles().screen, 0);
    // the tab is only a page behind a panel that pads itself; without this
    // the screen style's gap would double up with the panel's own
    lv_obj_set_style_pad_all(tab, 0, 0);
  }

  update_filament_tab();

  // These panels keep their existing foreground/background lifecycle, but
  // occupy only the page to the right of the shared navigation strip.
  for (auto *panel : {homing_panel.get_container(), extruder_panel.get_container(),
                      fan_panel.get_container(), led_panel.get_container(),
                      print_status_panel.get_container()}) {
    lv_obj_set_width(panel, lv_disp_get_hor_res(NULL) - tab_bar_w());
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, 0, 0);
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

void MainPanel::handle_emergency_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    LOG_TRACE("clicked emergency");
    ws.send_jsonrpc("printer.emergency_stop");
  }
}

void MainPanel::create_main(lv_obj_t * parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

  // Temperature readings and graph line up on the left; four compact actions
  // share one vertical column on the right.
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(42), LV_GRID_FR(58), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(main_cont, grid_main_col_dsc, grid_main_row_dsc);

  lv_obj_set_grid_cell(actions_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
  lv_obj_set_flex_flow(actions_cont, LV_FLEX_FLOW_COLUMN);
  for (ButtonContainer *b : {&homing_btn, &extrude_btn, &action_btn,
                             &led_btn}) {
    b->use_plain();
    lv_obj_set_width(b->get_container(), LV_PCT(100));
    lv_obj_set_height(b->get_container(), 0);
    lv_obj_set_flex_grow(b->get_container(), 1);
  }

  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(temp_cont, &styles().row, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(temp_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  // while printing, the status chip is the first row of the column (it is
  // hidden otherwise, and flex skips hidden children)
  lv_obj_set_parent(print_status_panel.get_mini_status(), temp_cont);
  lv_obj_move_to_index(print_status_panel.get_mini_status(), 0);

  // Keep the keypad wide enough for its three columns of keys.
  lv_obj_update_layout(main_cont);
  numpad.cover_from(lv_obj_get_content_width(main_cont) / 2);

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
  lv_obj_set_grid_cell(temp_chart_box, LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 1, 1);
  // room for the tick labels, which lv_chart draws to the left of the plot
  const lv_coord_t tick_gutter = scale_w(30);
  lv_obj_set_style_pad_left(temp_chart_box, tick_gutter, 0);
  lv_obj_set_style_pad_right(temp_chart_box, gap(), 0);
  // the tick labels centre on the top and bottom lines, so leave half a line of
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

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, chart_top);
  // minor_cnt must stay non-zero: lv_chart derives its tick count as
  // (major_cnt - 1) * minor_cnt and draws nothing at all when that is 0
  lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 5, 1, true, tick_gutter);

  // one guide per labelled value
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
  // Three readings fit the top row; additional sensors remain reachable by scrolling.
  const bool many_sensors = temp_sensors.size() > 3;
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

  lv_obj_scroll_to_y(temp_cont, 0, LV_ANIM_OFF);
  lv_obj_update_layout(temp_cont);
}

void MainPanel::create_fans(json &fans) {
  fan_panel.create_fans(fans);
}

void MainPanel::create_leds(json &leds) {
  std::lock_guard<std::mutex> lock(lv_lock);
  // runs again on every klipper reconnect, so a hidden button has to come back.
  // Leave the LED cell empty when absent, below Fans on the right.
  const bool has_leds = leds.is_array() && !leds.empty();
  if (has_leds) {
    led_btn.show();
  } else {
    led_btn.hide();
  }
  led_panel.init(leds);
  led_btn.set_image(led_panel.get_main_button_image());
}

void MainPanel::enable_spoolman() {
  if (spoolman_panel == NULL) {
    return;
  }
  spoolman_panel->init();
  std::lock_guard<std::mutex> lock(lv_lock);
  spoolman_available = true;
  if (mmu_panel != NULL) {
    mmu_panel->set_spoolman_available(true);
  }
  update_filament_tab();
}

void MainPanel::update_filament_tab() {
  if (filament_tab == NULL) {
    return;
  }
  auto *buttons = lv_tabview_get_tab_btns(tabview);
  const auto index = lv_obj_get_index(filament_tab);
  if (mmu_available || (spoolman_available && !SpoolmanPanel::hidden())) {
    lv_btnmatrix_clear_btn_ctrl(buttons, index, LV_BTNMATRIX_CTRL_DISABLED);
  } else {
    lv_btnmatrix_set_btn_ctrl(buttons, index, LV_BTNMATRIX_CTRL_DISABLED);
  }
  lv_obj_invalidate(buttons);
}

void MainPanel::enable_mmu() {
  if (filament_tab == NULL || mmu_panel == NULL) {
    return;
  }

  LOG_DEBUG("enabling mmu panel");
  std::lock_guard<std::mutex> lock(lv_lock);
  if (spoolman_panel != NULL) {
    spoolman_panel->detach();
  }
  mmu_panel->create(filament_tab);
  mmu_available = true;
  update_filament_tab();
}

// Klipper is offline or has no supported backend. Clear the previous session
// and restore Spoolman navigation when its browser is enabled.
void MainPanel::disable_mmu() {
  if (filament_tab == NULL || mmu_panel == NULL) {
    return;
  }

  LOG_DEBUG("disabling mmu panel");
  std::lock_guard<std::mutex> lock(lv_lock);
  const bool was_available = mmu_available;
  mmu_panel->clear();
  mmu_available = false;
  if (spoolman_panel != NULL) {
    spoolman_panel->background();
    if (!SpoolmanPanel::hidden()) {
      spoolman_panel->attach(filament_tab);
    }
  }
  if (was_available && lv_tabview_get_tab_act(tabview) == lv_obj_get_index(filament_tab)) {
    lv_tabview_set_act(tabview, lv_obj_get_index(main_tab), LV_ANIM_OFF);
  }
  update_filament_tab();
}
