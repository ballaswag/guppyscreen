#include "config.h"
#include "guppyscreen.h"
#include "state.h"
#include "theme.h"
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "lvgl/src/extra/libs/png/lodepng.h"
}

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <unistd.h>

// Exercise the panel's real asynchronous callbacks, with replies supplied in
// a controlled order instead of requiring a printer or a live network.
struct Request {
  std::string method;
  json params;
  std::function<void(json &)> callback;
};
static std::deque<Request> requests;
KWebSocketClient::KWebSocketClient(hv::EventLoopPtr loop) : WebSocketClient(loop), id(0) {}
KWebSocketClient::~KWebSocketClient() {}
int KWebSocketClient::connect(const char *, std::function<void()>, std::function<void()>,
    std::function<void()>, std::function<void()>, std::function<void()>) { return 0; }
int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params, std::function<void(json &)> cb) {
  requests.push_back({method, params, cb});
  return 1;
}
int KWebSocketClient::send_jsonrpc(const std::string &method, std::function<void(json &)> cb) {
  return send_jsonrpc(method, json::object(), cb);
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

static Request take(const char *method) {
  assert(!requests.empty());
  auto request = requests.front();
  requests.pop_front();
  assert(request.method == method);
  return request;
}
static void reply(const char *method, json data) { take(method).callback(data); }

static lv_obj_t *panel_root = nullptr;
static lv_obj_t *find_label(lv_obj_t *parent, const char *text, bool visible_only = false) {
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0 &&
      (!visible_only || lv_obj_is_visible(parent))) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *found = find_label(lv_obj_get_child(parent, i), text, visible_only)) {
      return found;
    }
  }
  return nullptr;
}
static lv_obj_t *label(const char *text) {
  auto *found = find_label(lv_layer_top(), text);
  if (found == nullptr) {
    found = find_label(panel_root == nullptr ? lv_scr_act() : panel_root, text);
  }
  if (found == nullptr) {
    std::fprintf(stderr, "Missing label: %s\n", text);
  }
  assert(found);
  return found;
}
static lv_obj_t *find_spinner(lv_obj_t *parent) {
  if (lv_obj_check_type(parent, &lv_spinner_class)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *spinner = find_spinner(lv_obj_get_child(parent, i))) {
      return spinner;
    }
  }
  return nullptr;
}
static lv_obj_t *find_action(lv_obj_t *parent, const char *text) {
  auto *found = find_label(parent, text);
  if (found != nullptr && lv_obj_is_visible(lv_obj_get_parent(found))) {
    return lv_obj_get_parent(found);
  }
  return nullptr;
}
static void click(const char *text, lv_event_code_t code = LV_EVENT_CLICKED) {
  lv_obj_update_layout(panel_root);
  lv_obj_update_layout(lv_layer_top());
  auto *found = find_action(lv_layer_top(), text);
  if (found == nullptr) {
    found = find_action(panel_root, text);
  }
  if (found == nullptr) {
    std::fprintf(stderr, "Missing visible label: %s\n", text);
  }
  assert(found);
  lv_event_send(found, code, nullptr);
}
static void choose(lv_obj_t *dialog, uint16_t index) {
  auto *buttons = lv_msgbox_get_btns(dialog);
  lv_btnmatrix_set_selected_btn(buttons, index);
  lv_event_send(buttons, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_timer_handler(); // close the dialog's asynchronous overlay
}
static void printer_state(const char *state) {
  json data = {{"print_stats", {{"state", state}}}};
  State::get_instance()->set_data("printer_state", data, "");
}
static json metadata(int seconds = 123) {
  return {{"result", {{"estimated_time", seconds}, {"filament_weight_total", 4.5},
                       {"size", 1024}, {"thumbnails", json::array()}}}};
}
static json file_list(int count = 18) {
  json files = json::array({{{"path", "parts/a \"b\".gcode"}, {"modified", 50}}});
  for (int i = 0; i < count; ++i) {
    files.push_back({{"path", "part_" + std::to_string(i) + ".gcode"}, {"modified", 1000 - i}});
  }
  return {{"result", files}};
}

static int screen_width, screen_height;
static std::vector<unsigned char> screenshot;
static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
  for (int y = area->y1; y <= area->y2; ++y) {
    for (int x = area->x1; x <= area->x2; ++x) {
      const auto rgb = lv_color_to32(*pixels++);
      const auto offset = (y * screen_width + x) * 4;
      screenshot[offset] = (rgb >> 16) & 255;
      screenshot[offset + 1] = (rgb >> 8) & 255;
      screenshot[offset + 2] = rgb & 255;
      screenshot[offset + 3] = 255;
    }
  }
  lv_disp_flush_ready(driver);
}

static void test_preview_cache(PrintPanel &panel) {
  char directory[] = "/tmp/grumpy-preview-cache-XXXXXX";
  assert(mkdtemp(directory));
  const std::string image_path = std::string(directory) + "/preview.png";
  unsigned char rgba[] = {40, 180, 100, 255};
  unsigned char *png = nullptr;
  size_t size = 0;
  assert(lodepng_encode32(&png, &size, rgba, 1, 1) == 0);
  {
    std::ofstream image_file(image_path, std::ios::binary);
    image_file.write(reinterpret_cast<const char *>(png), size);
  }
  lv_mem_free(png);
  json roots = json::array({{{"name", "gcodes"}, {"path", directory}}});
  State::get_instance()->set_data("roots", roots, "");
  auto preview = metadata();
  preview["result"]["thumbnails"] = json::array({{{"width", 1}, {"height", 1}, {"relative_path", "preview.png"}}});
  json listing = {{"result", json::array({
      {{"path", "cache_0.gcode"}, {"modified", 1000.0}, {"size", 1024}},
      {{"path", "cache_1.gcode"}, {"modified", 999.0}, {"size", 1024}},
      {{"path", "cache_2.gcode"}, {"modified", 998.0}, {"size", 1024}}})}};
  auto load_previews = [&] {
    int count = 0;
    while (!requests.empty()) {
      const bool has_image = requests.front().params["filename"] == "cache_0.gcode";
      reply("server.files.metadata", has_image ? preview : metadata());
      ++count;
    }
    return count;
  };
  reply("server.files.list", listing);
  auto *loading_card = lv_obj_get_parent(label("cache_0"));
  auto *loading_spinner = find_spinner(loading_card);
  assert(loading_spinner && lv_anim_get(loading_spinner, nullptr));
  assert(load_previews() == 3);
  auto *card = lv_obj_get_parent(label("cache_0"));
  auto *grid = lv_obj_get_parent(card);
  assert(!find_spinner(grid)); // success, including files without previews, stops animation
  auto *image = lv_obj_get_child(lv_obj_get_child(card, 0), 0);
  const void *source = lv_img_get_src(image);
  assert(lv_img_src_get_type(source) == LV_IMG_SRC_FILE);
  auto *decoded = _lv_img_cache_open(source, lv_color_black(), 0);
  assert(decoded && decoded->dec_dsc.img_data);
  const void *pixels = decoded->dec_dsc.img_data;
  click("cache_0", LV_EVENT_SHORT_CLICKED);
  click("Back");
  assert(requests.empty());

  click("cache_1", LV_EVENT_SHORT_CLICKED);
  click("Delete");
  choose(lv_obj_get_parent(find_label(lv_layer_top(), "Delete file")), 0);
  auto deletion = take("server.files.delete_file");
  assert(deletion.params["path"] == "gcodes/cache_1.gcode");
  json changed = {{"params", json::array({{{"item", {{"root", "gcodes"}}}}})}};
  // Moonraker's notification and the delete reply can both request a refresh.
  panel.handle_file_list_change(changed);
  json deleted = {{"result", {{"action", "delete_file"}}}};
  deletion.callback(deleted);
  listing["result"].erase(1);
  reply("server.files.list", listing);
  reply("server.files.list", listing);
  assert(requests.empty()); // no metadata requests for surviving files
  assert(!find_spinner(grid)); // cached cards do not restart their spinners
  assert(lv_obj_get_child_cnt(grid) == 2);
  assert(lv_obj_get_parent(label("cache_0")) == card);
  assert(lv_img_get_src(image) == source);
  assert(_lv_img_cache_open(source, lv_color_black(), 0)->dec_dsc.img_data == pixels);
  assert(find_label(card, "2m 3s"));

  // Modification times retain their subsecond precision; size also versions a file.
  for (const char *field : {"modified", "size"}) {
    listing["result"][0][field] = std::strcmp(field, "modified") == 0 ? 1000.25 : 2048.0;
    panel.handle_file_list_change(changed);
    reply("server.files.list", listing);
    assert(requests.size() == 1);
    assert(requests.front().params["filename"] == "cache_0.gcode");
    assert(find_spinner(card));
    reply("server.files.metadata", preview);
    assert(!find_spinner(grid));
    assert(requests.empty());
    assert(lv_obj_get_parent(label("cache_0")) == card);
  }
  decoded = _lv_img_cache_open(lv_img_get_src(image), lv_color_black(), 0);
  assert(decoded && decoded->dec_dsc.img_data);
  panel.background();
  assert(lv_obj_get_child_cnt(grid) == 0);
  assert(decoded->dec_dsc.src == nullptr); // decoded image memory was released
  panel.foreground();
  reply("server.files.list", listing);
  assert(load_previews() == 2); // a new visit starts with an empty cache

  // Neither late metadata nor a late list reply may repopulate a hidden panel.
  panel.background();
  panel.foreground();
  reply("server.files.list", listing);
  auto stale = take("server.files.metadata");
  loading_spinner = find_spinner(grid);
  assert(loading_spinner && lv_anim_get(loading_spinner, nullptr));
  panel.background();
  assert(!lv_anim_get(loading_spinner, nullptr));
  stale.callback(preview);
  assert(lv_obj_get_child_cnt(grid) == 0 && requests.empty());
  panel.foreground();
  stale = take("server.files.list");
  panel.background();
  panel.foreground();
  stale.callback(listing);
  assert(lv_obj_get_child_cnt(grid) == 0);
  reply("server.files.list", listing);
  assert(load_previews() == 2);
  panel.background();
  panel.foreground(); // leave the original fixture's list request pending
  std::remove(image_path.c_str());
  rmdir(directory);
}

int main(int argc, char **argv) {
  assert(Config::get_instance()->load("grumpyscreen.cfg"));
  screen_width = argc > 1 && std::strcmp(argv[1], "--small") == 0 ? 480 : 800;
  screen_height = screen_width == 480 ? 272 : 480;
  screenshot.resize(screen_width * screen_height * 4);
  lv_init();
  std::vector<lv_color_t> pixels(screen_width * 80);
  lv_disp_draw_buf_t buffer;
  lv_disp_draw_buf_init(&buffer, pixels.data(), nullptr, pixels.size());
  lv_disp_drv_t driver;
  lv_disp_drv_init(&driver);
  driver.hor_res = screen_width;
  driver.ver_res = screen_height;
  driver.dpi = LV_DPI_DEF * screen_width / 480;
  driver.draw_buf = &buffer;
  driver.flush_cb = flush;
  auto *display = lv_disp_drv_register(&driver);
  auto *theme = lv_theme_default_init(display, Theme::cfg_col("primary_colour"),
      Theme::cfg_col("secondary_colour"), true, Theme::scale_font(12));
  lv_disp_set_theme(display, theme);
  static lv_theme_t custom_theme = *theme;
  lv_theme_set_parent(&custom_theme, theme);
  lv_theme_set_apply_cb(&custom_theme, GuppyScreen::new_theme_apply_cb);
  lv_disp_set_theme(display, &custom_theme);

  KWebSocketClient ws(nullptr);
  std::mutex lock;
  auto &status = *new PrintStatusPanel(ws, lock, lv_scr_act());
  PrintPanel panel(ws, lock, status, nullptr);
  lv_obj_update_layout(lv_scr_act());
  panel_root = panel.get_container();
  requests.clear();
  printer_state("standby");
  panel.foreground();

  // Optional fixture from the simulator for inspecting the actual rendered UI.
  if (const char *fixture = std::getenv("PRINT_PANEL_FIXTURE")) {
    json data;
    std::ifstream(fixture) >> data;
    json roots = json::array({{{"name", "gcodes"}, {"path", data["root"]}}});
    State::get_instance()->set_data("roots", roots, "");
    reply("server.files.list", {{"result", data["files"]}});
    const char *limit_setting = std::getenv("PRINT_PANEL_METADATA_LIMIT");
    const int limit = limit_setting == nullptr ? -1 : std::atoi(limit_setting);
    int loaded = 0;
    while (!requests.empty() && (limit < 0 || loaded < limit)) {
      const auto path = requests.front().params["filename"].get<std::string>();
      reply("server.files.metadata", {{"result", data["metadata"][path]}});
      ++loaded;
    }
    if (const char *selected = std::getenv("PRINT_PANEL_FILE")) {
      click(selected, LV_EVENT_SHORT_CLICKED);
    }
    lv_refr_now(nullptr);
    if (const char *out = std::getenv("PRINT_PANEL_SCREENSHOT")) {
      unsigned char *png = nullptr;
      size_t size = 0;
      assert(lodepng_encode32(&png, &size, screenshot.data(), screen_width, screen_height) == 0);
      std::ofstream output(out, std::ios::binary);
      output.write(reinterpret_cast<const char *>(png), size);
      lv_mem_free(png);
    }
    std::puts("print panel screenshot saved");
    return 0;
  }

  test_preview_cache(panel);
  reply("server.files.list", file_list());
  auto *grid = lv_obj_get_parent(lv_obj_get_parent(label("part_0")));
  auto *first = lv_obj_get_parent(label("part_0"));
  auto *second = lv_obj_get_parent(label("part_1"));
  auto *third = lv_obj_get_parent(label("part_2"));
  lv_obj_update_layout(grid);
  assert(lv_obj_get_height(label("part_0")) == lv_font_get_line_height(Theme::scale_font(12)) + Theme::scale_r(4));
  assert(lv_obj_get_x(first) < lv_obj_get_x(second));
  assert(lv_obj_get_y(first) == lv_obj_get_y(second));
  assert((lv_obj_get_y(third) > lv_obj_get_y(first)) == (screen_width == 480));
  // Nested files are cards in the same gallery; folders never take a slot.
  assert(lv_obj_get_child_cnt(grid) == 19);
  assert(!find_label(grid, "parts"));
  assert(find_label(grid, "a \"b\""));
  lv_area_t grid_bounds, back_bounds;
  lv_obj_get_coords(grid, &grid_bounds);
  lv_obj_get_coords(lv_obj_get_parent(find_label(panel_root, "Back")), &back_bounds);
  assert(grid_bounds.x2 < back_bounds.x1);
  assert(back_bounds.y2 < screen_height);
  assert(lv_obj_get_scroll_bottom(grid) > 0);
  assert(find_spinner(first));
  assert(!find_spinner(lv_obj_get_parent(label("a \"b\"")))); // offscreen tiles do not animate
  reply("server.files.metadata", {{"error", {{"message", "Missing metadata"}}}});
  assert(!find_spinner(first));
  assert(requests.front().params["filename"] == "part_1.gcode");
  click("part_0", LV_EVENT_SHORT_CLICKED);
  assert(find_spinner(first)); // retrying a failed preview restarts its spinner
  // A failed tile can be retried while another preview is still in flight.
  reply("server.files.metadata", metadata());
  assert(requests.front().params["filename"] == "part_0.gcode");
  auto unknown = metadata();
  unknown["result"]["estimated_time"] = nullptr;
  unknown["result"]["filament_weight_total"] = nullptr;
  reply("server.files.metadata", unknown);
  assert(!find_spinner(first));
  assert(find_label(first, "Time ?"));
  assert(find_label(first, "? g"));
  assert(!lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  click("Back");
  int loaded = 0;
  while (!requests.empty()) {
    assert(requests.size() == 1); // at most one preview request at a time
    reply("server.files.metadata", metadata());
    ++loaded;
  }
  assert(loaded > 0 && loaded < 18); // offscreen files are not loaded eagerly
  assert(find_label(grid, "2m 3s"));
  assert(find_label(grid, "4.5 g"));
  lv_obj_scroll_to_y(grid, LV_COORD_MAX, LV_ANIM_OFF);
  lv_event_send(grid, LV_EVENT_SCROLL_END, nullptr);
  assert(!requests.empty());
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  const int saved_scroll = lv_obj_get_scroll_y(grid);
  json changed = {{"params", json::array({{{"item", {{"root", "gcodes"}}}}})}};
  panel.handle_file_list_change(changed);
  reply("server.files.list", file_list());
  assert(requests.empty());
  assert(lv_obj_get_scroll_y(grid) == saved_scroll);

  lv_obj_scroll_to_y(grid, saved_scroll / 2, LV_ANIM_OFF);
  lv_event_send(grid, LV_EVENT_SCROLL_END, nullptr);

  // A USB insertion refreshes the same gallery while an old preview is pending.
  auto stale = take("server.files.metadata");
  lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
  panel.handle_file_list_change(changed);
  auto with_usb = file_list();
  with_usb["result"].push_back({{"path", "USB/models/a \"b\".gcode"}, {"modified", 2000}});
  reply("server.files.list", with_usb);
  auto stale_data = metadata(999);
  stale.callback(stale_data);
  assert(requests.size() == 1); // stale replies do not start a duplicate request
  assert(requests.front().params["filename"] == "USB/models/a \"b\".gcode");
  reply("server.files.metadata", metadata(42));
  assert(!find_label(grid, "16m 39s"));
  assert(lv_obj_get_child_cnt(grid) == 20);
  assert(!find_label(grid, "USB"));
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  // The USB and printer files share a basename but retain their full paths.
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  assert(find_label(lv_layer_top(), "USB/models/a \"b\".gcode"));
  assert(lv_obj_is_visible(find_label(panel_root, "a \"b\""))); // the gallery remains behind the modal
  auto *modal = lv_obj_get_parent(lv_obj_get_parent(label("Print")));
  lv_obj_update_layout(modal);
  assert(lv_obj_get_width(modal) < screen_width);
  assert(lv_obj_get_height(modal) < screen_height);
  assert(!lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  click("Print");
  assert(take("printer.print.start").params["filename"] == "USB/models/a \"b\".gcode");
  status.background();
  panel.foreground();
  reply("server.files.list", with_usb);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  click("Delete");
  for (const char *state : {"printing", "paused"}) {
    printer_state(state);
    json update = {{"params", json::array({{{"print_stats", {{"state", state}}}}})}};
    panel.consume(update);
    lv_timer_handler();
    assert(!find_action(lv_layer_top(), "Print"));
    assert(!find_label(lv_layer_top(), "Delete file", true));
    panel.foreground();
    assert(lv_obj_get_index(panel_root) == 0);
    assert(requests.empty());
  }
  printer_state("standby");
  panel.foreground();
  reply("server.files.list", with_usb);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  click("a \"b\"", LV_EVENT_LONG_PRESSED);
  assert(!find_action(lv_layer_top(), "Print")); // holding has no separate action
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  click("Delete");
  assert(find_label(lv_layer_top(), "Delete file"));
  auto *dialog = lv_obj_get_parent(find_label(lv_layer_top(), "Delete file"));
  choose(dialog, 1); // cancel
  assert(requests.empty());
  click("Delete");
  dialog = lv_obj_get_parent(find_label(lv_layer_top(), "Delete file"));
  choose(dialog, 0);
  assert(requests.front().params["path"] == "gcodes/USB/models/a \"b\".gcode");
  reply("server.files.delete_file", {{"result", {{"action", "delete_file"}}}});
  reply("server.files.list", {{"result", json::array()}});
  assert(find_label(lv_scr_act(), "No G-code files"));
  assert(!find_action(lv_layer_top(), "Print"));

  // Unplugging USB while its selected file is loading returns to the gallery.
  panel.handle_file_list_change(changed);
  reply("server.files.list", {{"result", json::array({with_usb["result"].back()})}});
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  assert(lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  stale = take("server.files.metadata");
  panel.handle_file_list_change(changed);
  reply("server.files.list", file_list(1));
  stale_data = metadata(999);
  stale.callback(stale_data);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  assert(lv_obj_get_child_cnt(grid) == 2);
  assert(!find_label(lv_layer_top(), "USB/models/a \"b\".gcode", true));
  assert(!find_action(lv_layer_top(), "Print"));
  click("Back");
  panel.foreground();
  reply("server.files.list", {{"error", {{"message", "Test list failure"}}}});
  assert(find_label(lv_layer_top(), "File list failed"));
  std::puts("print card layout, merged USB files, lazy metadata, print and delete tests passed");
}
