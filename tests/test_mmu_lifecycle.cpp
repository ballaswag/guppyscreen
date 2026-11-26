#include "config.h"
#include "guppyscreen.h"
#include "icons.h"
#include "state.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// Control the order of startup replies, including replies from old sessions.
struct Request {
  std::string method;
  std::function<void(json &)> callback;
};
static std::vector<Request> requests;
KWebSocketClient::KWebSocketClient(hv::EventLoopPtr loop) : WebSocketClient(loop), id(0) {}
KWebSocketClient::~KWebSocketClient() {}
int KWebSocketClient::connect(const char *, std::function<void()>, std::function<void()>,
    std::function<void()>, std::function<void()>, std::function<void()>) { return 0; }
int KWebSocketClient::send_jsonrpc(const std::string &method, const json &, std::function<void(json &)> cb) {
  requests.push_back({method, std::move(cb)});
  return 1;
}
int KWebSocketClient::send_jsonrpc(const std::string &method, std::function<void(json &)> cb) {
  return send_jsonrpc(method, json::object(), std::move(cb));
}
int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer) {
  return send_jsonrpc(method, params, [consumer](json &data) { consumer->consume(data); });
}
int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params) {
  return send_jsonrpc(method, params, std::function<void(json &)>{});
}
int KWebSocketClient::send_jsonrpc(const std::string &method) { return send_jsonrpc(method, json::object()); }
int KWebSocketClient::gcode_script(const std::string &code) {
  return send_jsonrpc("printer.gcode.script", {{"script", code}});
}
void KWebSocketClient::set_gcode_sent_callback(std::function<void(const std::string &)>) {}
void KWebSocketClient::register_notify_update(NotifyConsumer *) {}
void KWebSocketClient::unregister_notify_update(NotifyConsumer *) {}
void KWebSocketClient::register_method_callback(std::string, std::string, std::function<void(json &)>) {}

static bool pending(const char *method) {
  return std::any_of(requests.begin(), requests.end(), [method](const Request &r) { return r.method == method; });
}

static Request take(const char *method) {
  auto it = std::find_if(requests.begin(), requests.end(), [method](const Request &r) { return r.method == method; });
  assert(it != requests.end());
  auto request = std::move(*it);
  requests.erase(it);
  return request;
}

static void reply(const char *method, json data) {
  auto request = take(method);
  assert(request.callback);
  request.callback(data);
}

static json components(bool spoolman) {
  return {{"result", {{"components", spoolman ? json::array({"spoolman"}) : json::array()}}}};
}

static lv_obj_t *find_label(lv_obj_t *parent, const char *text) {
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *found = find_label(lv_obj_get_child(parent, i), text)) {
      return found;
    }
  }
  return nullptr;
}

static lv_obj_t *find_icon(lv_obj_t *parent, const lv_img_dsc_t *source) {
  if (lv_obj_check_type(parent, &lv_img_class) && lv_img_get_src(parent) == source) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *found = find_icon(lv_obj_get_child(parent, i), source)) {
      return found;
    }
  }
  return nullptr;
}

static lv_obj_t *main_tabview() {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_scr_act()); ++i) {
    auto *child = lv_obj_get_child(lv_scr_act(), i);
    if (lv_obj_check_type(child, &lv_tabview_class)) {
      return child;
    }
  }
  return nullptr;
}

static void select_page(lv_obj_t *tabs, uint16_t index) {
  lv_tabview_set_act(tabs, index, LV_ANIM_OFF);
  lv_event_send(tabs, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_update_layout(tabs);
}

struct Fixture {
  KWebSocketClient ws{nullptr};
  std::mutex lock;
  MmuPanel mmu{ws, lock};
  SpoolmanPanel spoolman{ws, lock};
#ifdef MMU_BACKEND_AFC
  AfcBackend afc{ws};
#endif
#ifdef MMU_BACKEND_HH
  HhBackend hh{ws};
#endif
  // Like the app, these panels live until process exit.
  MainPanel &main = *new MainPanel(ws, lock, &spoolman, &mmu);
  InitPanel &init = *new InitPanel(main, lock);
  Fixture() {
#ifdef MMU_BACKEND_AFC
    mmu.add_backend(&afc);
#endif
#ifdef MMU_BACKEND_HH
    mmu.add_backend(&hh);
#endif
    main.create_panel();
    requests.clear();
  }
  lv_obj_t *tabs() { return main_tabview(); }
  lv_obj_t *pages() { return lv_tabview_get_content(tabs()); }
  bool disabled(int index) {
    return lv_btnmatrix_has_btn_ctrl(lv_tabview_get_tab_btns(tabs()), index, LV_BTNMATRIX_CTRL_DISABLED);
  }
};

static json printer_status(const char *object) {
  json status = {{"print_stats", {{"state", "standby"}}}};
  if (object != nullptr && std::strcmp(object, "AFC") == 0) {
    status["AFC"] = {{"lanes", json::array({"lane1"})}, {"current_state", "Idle"}};
    status["AFC_lane lane1"] = {{"prep", true}, {"load", true}, {"map", "T0"}};
  }
  if (object != nullptr && std::strcmp(object, "mmu") == 0) {
    status["mmu"] = {{"num_gates", 1}, {"gate_status", json::array({1})},
                     {"ttg_map", json::array({0})}, {"action", "Idle"}};
  }
  return status;
}

static Request start_ready(Fixture &f, const char *object) {
  f.init.klippy_ready(f.ws);
  assert(!pending("server.info"));
  reply("printer.info", {{"result", {{"state", "ready"}}}});
  assert(!pending("server.info")); // No Spoolman check before the object list.
  json objects = json::array({"print_stats"});
  if (object != nullptr) {
    objects.push_back(object);
    if (std::strcmp(object, "AFC") == 0) {
      objects.push_back("AFC_lane lane1");
    }
  }
  reply("printer.objects.list", {{"result", {{"objects", objects}}}});
  return take("server.info");
}

static void finish_ready(Fixture &f, const char *object) {
  reply("printer.objects.subscribe", {{"result", {{"status", printer_status(object)}}}});
}

static void disconnect(Fixture &f) {
  f.init.klippy_disconnected(f.ws);
  reply("printer.info", {{"result", {{"state", "disconnected"}}}});
}

static lv_obj_t *browser() {
  auto *reload = find_icon(lv_scr_act(), Icons::REFRESH_IMG);
  assert(reload);
  return lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(reload)));
}

static const char *supported_object() {
#ifdef MMU_BACKEND_AFC
  return "AFC";
#elif defined(MMU_BACKEND_HH)
  return "mmu";
#else
  return nullptr;
#endif
}

static void test_spoolman() {
  Fixture f;
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  auto *print_page = lv_obj_get_child(f.pages(), 1);
  auto *console_page = lv_obj_get_child(f.pages(), 2);
  auto *settings_page = lv_obj_get_child(f.pages(), 3);
  assert(f.disabled(1));
  assert(lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));

  f.init.websocket_connected(f.ws);
  reply("printer.info", {{"result", {{"state", "startup"}}}});
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  assert(!pending("server.info") && !pending("printer.objects.list"));

  auto absent = start_ready(f, nullptr);
  auto off = components(false);
  absent.callback(off);
  finish_ready(f, nullptr);
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  assert(!f.disabled(lv_obj_get_index(print_page)));
  disconnect(f);

  // A positive reply from a startup interrupted by a restart cannot add a slot.
  auto stale = start_ready(f, nullptr);
  disconnect(f);
  auto on = components(true);
  stale.callback(on);
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  // Discard the old pending subscription; its generation is tested below too.
  auto stale_subscription = take("printer.objects.subscribe");

  auto detected = start_ready(f, nullptr);
  finish_ready(f, nullptr);
  select_page(f.tabs(), lv_obj_get_index(console_page));
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  // Spoolman can be discovered after the printer subscription has completed.
  detected.callback(on);
  assert(lv_obj_get_child_cnt(f.pages()) == 5);
  auto *page = lv_obj_get_child(f.pages(), 1);
  assert(lv_obj_get_parent(browser()) == page);
  assert(lv_obj_get_index(print_page) == 2 && lv_obj_get_index(console_page) == 3);
  assert(lv_obj_get_index(settings_page) == 4);
  assert(lv_tabview_get_tab_act(f.tabs()) == lv_obj_get_index(console_page));
  assert(!f.disabled(1) && !f.disabled(2));
  detected.callback(on);
  assert(lv_obj_get_child_cnt(f.pages()) == 5); // No duplicate on repeat detection.

  select_page(f.tabs(), 1);
  assert(lv_obj_is_visible(browser()));
  assert(!lv_obj_is_visible(lv_obj_get_parent(find_label(browser(), "Back"))));
  disconnect(f);
  assert(f.main.home_active() && f.disabled(1));
  assert(lv_obj_get_child(f.pages(), 1) == page);
  assert(lv_obj_get_child_cnt(f.pages()) == 5);
  auto initial = json{{"result", {{"status", printer_status(nullptr)}}}};
  stale_subscription.callback(initial);
  detected.callback(on);
  assert(f.disabled(1));

  // Even an MMU appearing later cannot replace the already selected Spoolman.
  auto missing = start_ready(f, supported_object());
  missing.callback(off);
  finish_ready(f, supported_object());
  assert(lv_obj_get_child_cnt(f.pages()) == 5 && f.disabled(1));
  assert(lv_obj_get_parent(browser()) == page);
  assert(!find_label(page, "MMU Standby"));
  disconnect(f);

  auto restored = start_ready(f, supported_object());
  finish_ready(f, supported_object());
  restored.callback(on);
  assert(!f.disabled(1) && lv_obj_get_child(f.pages(), 1) == page);
  f.init.websocket_disconnected(f.ws);
  assert(lv_obj_get_child_cnt(f.pages()) == 5 && f.disabled(1));
  assert(f.disabled(lv_obj_get_index(print_page)));

  std::puts("Dynamic Spoolman startup, persistence and stale-reply tests passed");
}

static void test_mmu() {
  Fixture f;
  assert(lv_obj_get_child_cnt(f.pages()) == 4);
  auto *console_page = lv_obj_get_child(f.pages(), 2);
  auto on = components(true);
  auto off = components(false);
  std::vector<std::pair<const char *, MmuBackend *>> backends;
#ifdef MMU_BACKEND_AFC
  backends.push_back({"AFC", &f.afc});
#endif
#ifdef MMU_BACKEND_HH
  backends.push_back({"mmu", &f.hh});
#endif
#ifdef MMU_BACKEND_AFC
  backends.push_back({"AFC", &f.afc});
#endif
  lv_obj_t *page = nullptr;
  for (const auto &entry : backends) {
    select_page(f.tabs(), lv_obj_get_index(console_page));
    auto spoolman = start_ready(f, entry.first);
    // MMU is added before any Spoolman reply, preserving the current page.
    assert(lv_obj_get_child_cnt(f.pages()) == 5 && !f.disabled(1));
    assert(lv_tabview_get_tab_act(f.tabs()) == lv_obj_get_index(console_page));
    if (page == nullptr) {
      page = lv_obj_get_child(f.pages(), 1);
    }
    assert(lv_obj_get_child(f.pages(), 1) == page);
    assert(lv_obj_get_parent(browser()) == lv_scr_act());
    assert(!find_icon(page, Icons::REFRESH_IMG));
    finish_ready(f, entry.first);
    select_page(f.tabs(), 1);
    auto *spoolman_icon = find_icon(page, Icons::NAV_SPOOLMAN);
    assert(spoolman_icon);
    auto *shortcut = lv_obj_get_parent(spoolman_icon);
    assert(lv_obj_has_state(shortcut, LV_STATE_DISABLED));
    spoolman.callback(on);
    assert(!lv_obj_has_state(shortcut, LV_STATE_DISABLED));
    assert(lv_obj_get_child_cnt(f.pages()) == 5 && !f.disabled(1));
    lv_event_send(shortcut, LV_EVENT_CLICKED, nullptr);
    assert(lv_obj_is_visible(browser()));
    auto *back = lv_obj_get_parent(find_label(browser(), "Back"));
    assert(lv_obj_is_visible(back));
    lv_event_send(back, LV_EVENT_CLICKED, nullptr);
    assert(lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));
    assert(lv_tabview_get_tab_act(f.tabs()) == 1);

    bool picked = false;
    f.mmu.pick_spool("S1", -1, [&](int) { picked = true; });
    assert(lv_obj_is_visible(find_label(browser(), "Spool for S1")));
    lv_event_send(back, LV_EVENT_CLICKED, nullptr);
    assert(!picked && lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));

    auto *label = find_label(page, "S1 (T0)");
    assert(label);
    auto *card = lv_obj_get_parent(label);
    while (!lv_obj_has_flag(card, LV_OBJ_FLAG_CLICKABLE)) {
      card = lv_obj_get_parent(card);
    }
    lv_event_send(card, LV_EVENT_CLICKED, nullptr);
    auto *edit = lv_obj_get_child(lv_scr_act(), -1);
    assert(!lv_obj_has_flag(edit, LV_OBJ_FLAG_HIDDEN));
    assert((find_label(edit, LV_SYMBOL_CLOSE) != nullptr) == entry.second->can_clear_colour());
    f.mmu.pick_spool("S1", -1, [&](int) { picked = true; });
    disconnect(f);
    assert(f.main.home_active() && f.disabled(1));
    assert(lv_obj_has_flag(edit, LV_OBJ_FLAG_HIDDEN));
    assert(!picked && lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));
    assert(!entry.second->changed);
    assert(lv_obj_has_state(shortcut, LV_STATE_DISABLED));
    spoolman.callback(on);
    assert(f.disabled(1) && lv_obj_has_state(shortcut, LV_STATE_DISABLED));
    assert(!f.mmu.open_tool_map(json::object(), "test.gcode", [] {}));
  }

  if (page != nullptr) {
    // Removing the MMU must not replace its button with Spoolman on restart.
    auto disappeared = start_ready(f, nullptr);
    disappeared.callback(on);
    finish_ready(f, nullptr);
    assert(lv_obj_get_child(f.pages(), 1) == page && f.disabled(1));
    assert(lv_obj_get_child_cnt(f.pages()) == 5);
    assert(lv_obj_get_parent(browser()) == lv_scr_act());
    disconnect(f);

    // The MMU can return with Spoolman removed: enable MMU alone.
    auto no_spoolman = start_ready(f, backends.front().first);
    finish_ready(f, backends.front().first);
    no_spoolman.callback(off);
    assert(!f.disabled(1));
    auto *shortcut = lv_obj_get_parent(find_icon(page, Icons::NAV_SPOOLMAN));
    assert(lv_obj_has_state(shortcut, LV_STATE_DISABLED));
    lv_event_send(shortcut, LV_EVENT_CLICKED, nullptr);
    assert(lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));
  }
  std::puts("Dynamic MMU detection, priority, controls and reconnect tests passed");
}

int main(int argc, char **argv) {
  assert(Config::get_instance()->load("grumpyscreen.cfg"));
  const std::string mode = argc > 1 ? argv[1] : "";
  const bool small = mode == "--small" || mode == "--spoolman-small";
  lv_init();
  static lv_color_t pixels[800];
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels, nullptr, 800);
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  driver.hor_res = small ? 480 : 800;
  driver.ver_res = small ? 272 : 480;
  driver.draw_buf = &buffer;
  lv_disp_drv_register(&driver);

  if (mode == "--startup") {
    new GuppyScreen();
    auto *tabs = main_tabview();
    assert(tabs);
    assert(lv_obj_get_child_cnt(lv_tabview_get_content(tabs)) == 4);
    assert(lv_btnmatrix_has_btn_ctrl(lv_tabview_get_tab_btns(tabs), 1, LV_BTNMATRIX_CTRL_DISABLED));
    assert(lv_obj_has_flag(browser(), LV_OBJ_FLAG_HIDDEN));
    std::puts("Startup has neither MMU nor Spoolman navigation placeholder");
  } else if (mode == "--spoolman" || mode == "--spoolman-small") {
    test_spoolman();
  } else {
    test_mmu();
  }
}
