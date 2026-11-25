#include "config.h"
#include "guppyscreen.h"
#include "notification_banner.h"
#include "state.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>

static lv_obj_t *find_label(lv_obj_t *parent, const char *text) {
  if (lv_obj_check_type(parent, &lv_label_class) &&
      std::strcmp(lv_label_get_text(parent), text) == 0) return parent;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *found = find_label(lv_obj_get_child(parent, i), text)) return found;
  }
  return nullptr;
}

static lv_obj_t *main_tabview() {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_scr_act()); ++i) {
    auto *child = lv_obj_get_child(lv_scr_act(), i);
    if (lv_obj_check_type(child, &lv_tabview_class)) return child;
  }
  return nullptr;
}

static void test_visibility_config() {
  auto *config = Config::get_instance();
  assert(config->load("grumpyscreen.cfg"));
  assert(MmuPanel::enabled());
  assert(!SpoolmanPanel::hidden());

  const auto path = "/tmp/grumpy-mmu-config-" + std::to_string(getpid()) + ".cfg";
  // Both visibility options are controlled independently by the override.
  for (bool hide_mmu : {false, true}) {
    for (bool hide_spoolman : {false, true}) {
      {
        std::ofstream out(path);
        out << "[ui]\nhide_mmu: " << (hide_mmu ? "true" : "false")
            << "\nhide_spoolman: " << (hide_spoolman ? "true" : "false") << '\n';
      }
      assert(config->load_override(path));
      assert(MmuPanel::enabled() == !hide_mmu);
      assert(SpoolmanPanel::hidden() == hide_spoolman);
    }
  }

  // Legacy backend settings no longer select a driver or hide the tab.
  {
    std::ofstream out(path);
    out << "[mmu]\nbackend: none\n";
  }
  assert(config->load(path));
  assert(MmuPanel::enabled());
  assert(!SpoolmanPanel::hidden());
  assert(config->load("grumpyscreen.cfg"));
  assert(MmuPanel::enabled());
  assert(!SpoolmanPanel::hidden());
  std::remove(path.c_str());
}

static void load_printer(const char *object) {
  auto *state = State::get_instance();
  state->reset();
  json objects = {{"objects", json::array({"print_stats"})}};
  if (object) objects["objects"].push_back(object);
  if (object && std::strcmp(object, "AFC") == 0) objects["objects"].push_back("AFC_lane lane1");
  state->set_data("printer_objs", objects, "");
  json status = {{"print_stats", {{"state", "standby"}}}};
  if (object && std::strcmp(object, "AFC") == 0) {
    status["AFC"] = {{"lanes", json::array({"lane1"})}, {"current_state", "Idle"}};
    status["AFC_lane lane1"] = {{"prep", true}, {"load", true}, {"map", "T0"}};
  }
  if (object && std::strcmp(object, "mmu") == 0) {
    status["mmu"] = {{"num_gates", 1}, {"gate_status", json::array({1})},
                     {"ttg_map", json::array({0})}, {"action", "Idle"}};
  }
  state->set_data("printer_state", status, "");
}

int main(int argc, char **argv) {
  test_visibility_config();
  const bool hidden = argc > 1 && std::strcmp(argv[1], "--hidden") == 0;
  if (hidden) {
    const auto path = "/tmp/grumpy-hidden-config-" + std::to_string(getpid()) + ".cfg";
    {
      std::ofstream out(path);
      out << "[ui]\nhide_mmu: true\nhide_spoolman: true\n";
    }
    assert(Config::get_instance()->load_override(path));
    std::remove(path.c_str());
  }

  lv_init();
  static lv_color_t pixels[800];
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  driver.hor_res = 800;
  driver.ver_res = 480;
  driver.draw_buf = &buffer;
  lv_disp_drv_register(&driver);

  // The app lives until process exit, as it does in production. Run startup
  // checks separately so its tabview does not overlap the reconnect fixture.
  if (argc > 1) {
    new GuppyScreen();
    auto *tabview = main_tabview();
    assert(tabview);
    auto *pages = lv_tabview_get_content(tabview);
    assert(lv_obj_get_child_cnt(pages) == (hidden ? 3 : 5));
    if (!hidden) {
      assert(lv_btnmatrix_has_btn_ctrl(lv_tabview_get_tab_btns(tabview), 1,
                                       LV_BTNMATRIX_CTRL_DISABLED));
      assert(lv_obj_get_child_cnt(lv_obj_get_child(pages, 1)) == 0);
    }
    std::puts(hidden ? "hidden MMU startup tests passed" : "visible MMU startup tests passed");
    return 0;
  }

  KWebSocketClient ws(nullptr);
  std::mutex lock;
#ifdef MMU_BACKEND_AFC
  AfcBackend afc(ws);
#endif
#ifdef MMU_BACKEND_HH
  HhBackend hh(ws);
#endif
  MmuPanel mmu(ws, lock);
#ifdef MMU_BACKEND_AFC
  mmu.add_backend(&afc);
#endif
#ifdef MMU_BACKEND_HH
  mmu.add_backend(&hh);
#endif
  SpoolmanPanel spoolman(ws, lock);
  auto &main = *new MainPanel(ws, lock, &spoolman, &mmu);
  main.create_panel();
  auto *tabview = main_tabview();
  auto *button = lv_tabview_get_tab_btns(tabview);
  auto *page = lv_obj_get_child(lv_tabview_get_content(tabview), 1);
  auto disabled = [&] { return lv_btnmatrix_has_btn_ctrl(button, 1, LV_BTNMATRIX_CTRL_DISABLED); };

  load_printer(nullptr);
  assert(mmu.select_backend() == nullptr);
  assert(disabled());
  assert(lv_obj_get_child_cnt(page) == 0);

  auto *print_button = lv_obj_get_parent(find_label(tabview, "Print"));
  for (const char *state : {"printing", "paused", "complete", "cancelled", "error", "standby"}) {
    json printer = {{"print_stats", {{"state", state}}}};
    State::get_instance()->set_data("printer_state", printer, "");
    json update = {{"params", json::array({printer})}};
    main.consume(update);
    main.set_klipper_ready(true);
    const bool busy = std::strcmp(state, "printing") == 0 || std::strcmp(state, "paused") == 0;
    assert(lv_obj_has_state(print_button, LV_STATE_DISABLED) == busy);
    main.set_klipper_ready(false);
    assert(lv_obj_has_state(print_button, LV_STATE_DISABLED));
  }

  auto connect = [&](const char *object, MmuBackend *expected) {
    load_printer(object);
    assert(mmu.select_backend() == expected);
    main.enable_mmu();
    json initial = json::object();
    main.init(initial);
    main.set_klipper_ready(true);
    assert(!disabled());
    assert(lv_obj_get_child_cnt(page) > 0);
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);
    lv_obj_update_layout(tabview);
    // Opening a slot builds the vendor-specific colour controls.
    auto *label = find_label(page, "S1 (T0)");
    assert(label);
    auto *card = lv_obj_get_parent(label);
    while (!lv_obj_has_flag(card, LV_OBJ_FLAG_CLICKABLE)) card = lv_obj_get_parent(card);
    lv_event_send(card, LV_EVENT_CLICKED, nullptr);
    auto *edit = lv_obj_get_child(lv_scr_act(), -1);
    assert(!lv_obj_has_flag(edit, LV_OBJ_FLAG_HIDDEN));
    assert((find_label(edit, LV_SYMBOL_CLOSE) != nullptr) == expected->can_clear_colour());
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);
    main.set_klipper_ready(false);
    assert(disabled());
    assert(main.home_active());
    assert(lv_obj_has_flag(edit, LV_OBJ_FLAG_HIDDEN));
    assert(!expected->changed);
    assert(!mmu.open_tool_map(json::object(), "test.gcode", [] {}));
  };
#ifdef MMU_BACKEND_AFC
  connect("AFC", &afc);
#endif
#ifdef MMU_BACKEND_HH
  connect("mmu", &hh);
#endif
#ifdef MMU_BACKEND_AFC
  connect("AFC", &afc); // Switching back must also rebuild the colour controls.
#endif
  load_printer(nullptr);
  assert(mmu.select_backend() == nullptr);
  main.disable_mmu();
  assert(disabled());

  std::puts("MMU detection, navigation and reconnect tests passed");
}
