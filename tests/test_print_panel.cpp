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
#include <sys/stat.h>
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

// Model the two Moonraker responses: the recursive G-code list and the root's
// directory entries, which can include an empty USB directory.
static void reply_files(json data, bool empty_usb = false) {
  const bool success = data.contains("result") && data["result"].is_array();
  bool usb = empty_usb;
  if (success) {
    for (const auto &file : data["result"]) {
      usb = usb || file["path"].get<std::string>().compare(0, 4, "usb/") == 0;
    }
  }
  reply("server.files.list", data);
  if (success) {
    auto directory = take("server.files.get_directory");
    assert(directory.params == json({{"path", "gcodes"}}));
    json response = {{"result", {{"dirs", usb ? json::array({{{"dirname", "usb"}}}) : json::array()}}}};
    directory.callback(response);
  }
}

static lv_obj_t *panel_root = nullptr;
static lv_obj_t *find_label(lv_obj_t *parent, const char *text, bool visible_only = false) {
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0 &&
      (!visible_only || lv_obj_is_visible(lv_obj_get_parent(parent)))) {
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
  auto *found = find_label(lv_layer_top(), text, true);
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
  auto *found = find_label(parent, text, true);
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
                       {"filament_type", "PLA"}, {"size", 1024}, {"thumbnails", json::array()}}}};
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

static void test_tile_captions(PrintPanel &panel, PrintStatusPanel &status) {
  struct Example {
    std::string filename;
    std::string caption;
    std::string material;
    json metadata_type;
    int seconds = 123;
    std::string time = "2m3s";
  };
  const std::vector<Example> examples = {
    {"pizza-Body_PLA_37m33s.gcode", "pizza-Body", "PLA", "PETG", 2253, "37m33s"},
    {"part_with_x_PETG-CF_2h7m.gcode", "part_with_x", "PETG-CF", "PETG-CF", 7620, "2h7m"},
    {"tower_PA6-CF_1d2h3m4s.gcode", "tower", "PA6-CF", "PA6-CF", 93784, "1d2h"},
    {"clip_PLA+_45s.gcode", "clip", "PLA+", "PLA+", 45, "45s"},
    {"part_ASA-Aero_2h.gcode", "part", "ASA-Aero", "ASA-Aero"},
    {"part_ABS_0s.GCODE", "part", "ABS", "ABS"},
    {"version.v2_PLA_37m.gcode", "version.v2", "PLA", "PLA"},
    {"plain_part.gcode", "plain_part", "TPU", "TPU"},
    {"part_PLA_37m33s_extra.gcode", "part_PLA_37m33s_extra", "PLA", "PLA"},
    {"part_PLA_37m_bad.gcode", "part_PLA_37m_bad", "PLA", "PLA"},
    {"part_PLA_.gcode", "part_PLA_", "PLA", "PLA"},
    {"part_lower_37m33s.gcode", "part_lower_37m33s", "PLA", "PLA"},
    {"part_37m33s.gcode", "part_37m33s", "PLA", "PLA"},
    {"mixed.gcode", "mixed", "PLA/PETG", json::array({"PLA", "PLA", "PETG"})},
    {"mixed.gcode", "mixed", "PLA/PETG", "[\"PLA\", \"PLA\", \"PETG\"]"},
    {"mixed.gcode", "mixed", "PLA/PETG", "PLA; PETG"},
    {"unknown.gcode", "unknown", "Unknown", nullptr},
  };
  bool check_start_path = true;
  for (const auto &example : examples) {
    const std::string path = "USB/models/" + example.filename;
    reply_files({{"result", json::array({{{"path", path}, {"modified", 1000}, {"size", 1024}}})}});
    auto *name = label(example.caption.c_str());
    auto *card = lv_obj_get_parent(name);
    auto *footer = lv_obj_get_child(card, -1);
    if (check_start_path) {
      assert(find_label(footer, "PLA")); // parsed material appears before metadata arrives
    }
    assert(requests.front().params["filename"] == path);
    auto details = metadata(example.seconds);
    details["result"]["filament_type"] = example.metadata_type;
    reply("server.files.metadata", details);
    assert(requests.empty());
    assert(std::strcmp(lv_label_get_text(name), example.caption.c_str()) == 0);
    auto *material = find_label(footer, example.material.c_str());
    auto *time = find_label(footer, example.time.c_str());
    auto *weight = find_label(footer, "4.5 g");
    assert(material && time && weight);
    lv_obj_update_layout(footer);
    // All three fields share the second line, without hiding time or weight.
    assert(lv_obj_get_y(material) == lv_obj_get_y(time));
    assert(lv_obj_get_y(time) == lv_obj_get_y(weight));
    assert(lv_obj_get_x(material) + lv_obj_get_width(material) <= lv_obj_get_x(time));
    assert(lv_obj_get_x(time) + lv_obj_get_width(time) <= lv_obj_get_x(weight));
    assert(lv_obj_get_x(weight) + lv_obj_get_width(weight) <=
        lv_obj_get_width(footer) - lv_obj_get_style_pad_right(footer, 0));
    if (check_start_path) {
      click(example.caption.c_str(), LV_EVENT_SHORT_CLICKED);
      assert(find_label(lv_layer_top(), path.c_str()));
      click("Print");
      assert(take("printer.print.start").params["filename"] == path);
      status.background();
      check_start_path = false;
    }
    panel.background();
    panel.foreground();
  }

  const std::string long_name = "a_very_long_model_name_that_should_scroll_across_the_tile_instead_of_being_shortened";
  const std::string long_path = long_name + "_PLA_37m33s.gcode";
  const std::string last_name = "another_long_model_name_in_a_folder_that_only_animates_when_scrolled_into_view";
  const std::string last_material = "A-MATERIAL-NAME-WIDER-THAN-THE-TILE";
  auto listing = file_list();
  listing["result"][1]["path"] = long_path;
  listing["result"][0]["path"] = "parts/" + last_name + "_" + last_material + "_1h2m.gcode";
  reply_files(listing);
  auto *first = label(long_name.c_str());
  auto *last = label(last_name.c_str());
  auto *material = find_label(lv_obj_get_parent(last), last_material.c_str());
  assert(material);
  auto *short_name = label("part_1");
  assert(lv_label_get_long_mode(first) == LV_LABEL_LONG_SCROLL_CIRCULAR);
  assert(lv_anim_get(first, nullptr));
  assert(!lv_anim_get(short_name, nullptr));
  assert(!lv_anim_get(last, nullptr));
  assert(!lv_anim_get(material, nullptr));
  click(long_name.c_str(), LV_EVENT_SHORT_CLICKED);
  auto *modal_name = find_label(lv_layer_top(), long_path.c_str());
  assert(modal_name);
  lv_obj_update_layout(lv_layer_top());
  assert(lv_label_get_long_mode(modal_name) == LV_LABEL_LONG_SCROLL_CIRCULAR);
  assert(lv_anim_get(modal_name, nullptr)); // scrolls even before metadata arrives
  reply("server.files.metadata", metadata());
  assert(lv_anim_get(modal_name, nullptr)); // still scrolls when details arrive
  click("Back");
  assert(!lv_anim_get(modal_name, nullptr));
  click("part_1", LV_EVENT_SHORT_CLICKED);
  auto *short_modal_name = find_label(lv_layer_top(), "part_1.gcode");
  assert(short_modal_name);
  lv_obj_update_layout(lv_layer_top());
  assert(!lv_anim_get(short_modal_name, nullptr));
  click("Back");
  auto *grid = lv_obj_get_parent(lv_obj_get_parent(first));
  lv_obj_scroll_to_y(grid, LV_COORD_MAX, LV_ANIM_OFF);
  assert(!lv_anim_get(first, nullptr));
  assert(lv_anim_get(last, nullptr));
  assert(lv_anim_get(material, nullptr));
  auto pending = take("server.files.metadata");
  panel.background();
  assert(!lv_anim_get(last, nullptr));
  assert(!lv_anim_get(material, nullptr));
  auto stale = metadata();
  pending.callback(stale);
  assert(requests.empty());
  panel.foreground();
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
  reply_files(listing);
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
  reply_files(listing);
  reply_files(listing);
  assert(requests.empty()); // no metadata requests for surviving files
  assert(!find_spinner(grid)); // cached cards do not restart their spinners
  assert(lv_obj_get_child_cnt(grid) == 2);
  assert(lv_obj_get_parent(label("cache_0")) == card);
  assert(lv_img_get_src(image) == source);
  assert(_lv_img_cache_open(source, lv_color_black(), 0)->dec_dsc.img_data == pixels);
  assert(find_label(card, "PLA"));
  assert(find_label(card, "2m3s"));

  // Modification times retain their subsecond precision; size also versions a file.
  for (const char *field : {"modified", "size"}) {
    listing["result"][0][field] = std::strcmp(field, "modified") == 0 ? 1000.25 : 2048.0;
    panel.handle_file_list_change(changed);
    reply_files(listing);
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
  reply_files(listing);
  assert(load_previews() == 2); // a new visit starts with an empty cache

  // Neither late metadata nor a late list reply may repopulate a hidden panel.
  panel.background();
  panel.foreground();
  reply_files(listing);
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
  reply_files(listing);
  assert(load_previews() == 2);
  panel.background();
  panel.foreground(); // leave the original fixture's list request pending
  std::remove(image_path.c_str());
  rmdir(directory);
}

static MainPanel &create_navigation(KWebSocketClient &ws, std::mutex &lock) {
  auto &spoolman = *new SpoolmanPanel(ws, lock);
  auto &mmu = *new MmuPanel(ws, lock);
  auto &main = *new MainPanel(ws, lock, &spoolman, &mmu);
  main.create_panel();
  main.set_klipper_ready(true);
  requests.clear();
  return main;
}

static lv_obj_t *navigation() {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_scr_act()); ++i) {
    auto *child = lv_obj_get_child(lv_scr_act(), i);
    if (lv_obj_check_type(child, &lv_tabview_class)) {
      return child;
    }
  }
  assert(false);
  return nullptr;
}

static lv_indev_data_t pointer_input;
static lv_indev_t *create_pointer() {
  static lv_indev_drv_t driver; // LVGL retains the driver pointer
  lv_indev_drv_init(&driver);
  driver.type = LV_INDEV_TYPE_POINTER;
  driver.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *data) { *data = pointer_input; };
  return lv_indev_drv_register(&driver);
}

static void pointer_event(lv_indev_t *pointer, int x, int y, lv_indev_state_t state) {
  pointer_input.point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
  pointer_input.state = state;
  lv_indev_read_timer_cb(pointer->driver->read_timer);
}

static void select_page(lv_obj_t *tabs, uint16_t index) {
  lv_obj_update_layout(tabs);
  auto *buttons = lv_tabview_get_tab_btns(tabs);
  const auto &area = reinterpret_cast<lv_btnmatrix_t *>(buttons)->button_areas[index];
  const int x = buttons->coords.x1 + (area.x1 + area.x2) / 2;
  const int y = buttons->coords.y1 + (area.y1 + area.y2) / 2;
  auto *pointer = create_pointer();
  for (auto state : {LV_INDEV_STATE_PRESSED, LV_INDEV_STATE_RELEASED}) {
    pointer_event(pointer, x, y, state);
  }
  lv_indev_delete(pointer);
  lv_obj_update_layout(tabs);
}

static lv_obj_t *file_tabs() {
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(panel_root); ++i) {
    auto *child = lv_obj_get_child(panel_root, i);
    if (lv_obj_check_type(child, &lv_tabview_class)) {
      return child;
    }
  }
  assert(false);
  return nullptr;
}

static lv_obj_t *gallery() {
  auto *tabs = file_tabs();
  auto *page = lv_obj_get_child(lv_tabview_get_content(tabs), lv_tabview_get_tab_act(tabs));
  return lv_obj_get_child(lv_obj_get_child(page, 0), 0);
}

static void test_source_tabs(PrintPanel &panel) {
  auto tab_count = [] {
    return lv_obj_get_child_cnt(lv_tabview_get_content(file_tabs()));
  };
  json changed = {{"params", json::array({{{"item", {{"root", "gcodes"}}}}})}};
  reply_files({{"result", json::array()}});
  assert(tab_count() == 1);
  assert(std::strcmp(lv_btnmatrix_get_btn_text(lv_tabview_get_tab_btns(file_tabs()), 0), "Printer") == 0);
  assert(find_label(gallery(), "No G-code files"));
  assert(!lv_obj_is_visible(lv_tabview_get_tab_btns(file_tabs())));
  lv_obj_update_layout(panel_root);
  const int full_gallery_height = lv_obj_get_height(gallery());
  const int full_gallery_top = gallery()->coords.y1;

  panel.handle_file_list_change(changed);
  reply_files({{"result", json::array()}}, true);
  assert(tab_count() == 2); // directory existence is sufficient, even with no files
  assert(std::strcmp(lv_btnmatrix_get_btn_text(lv_tabview_get_tab_btns(file_tabs()), 1), "USB") == 0);
  assert(lv_obj_is_visible(lv_tabview_get_tab_btns(file_tabs())));
  lv_obj_update_layout(panel_root);
  assert(lv_obj_get_height(gallery()) < full_gallery_height);
  assert(gallery()->coords.y1 > full_gallery_top);
  select_page(file_tabs(), 1);
  assert(lv_tabview_get_tab_act(file_tabs()) == 1);
  assert(find_label(gallery(), "No G-code files"));
  assert(requests.empty());

  // Losing the USB directory removes the header and restores the gallery space.
  panel.handle_file_list_change(changed);
  reply_files({{"result", json::array()}});
  assert(tab_count() == 1 && lv_tabview_get_tab_act(file_tabs()) == 0);
  assert(!lv_obj_is_visible(lv_tabview_get_tab_btns(file_tabs())));
  assert(lv_obj_get_height(gallery()) == full_gallery_height);
  assert(gallery()->coords.y1 == full_gallery_top);

  auto listing = file_list();
  for (const auto &path : {"usb_backup/backup.gcode", "parts/usb/nested.gcode", "usb.gcode"}) {
    listing["result"].push_back({{"path", path}, {"modified", 25}});
  }
  for (int i = 0; i < 20; ++i) {
    listing["result"].push_back({{"path", "usb/models/usb_" + std::to_string(i) + ".gcode"},
                                 {"modified", 2000 - i}});
  }
  panel.handle_file_list_change(changed);
  reply_files(listing);
  auto *grid = gallery();
  assert(lv_obj_get_child_cnt(grid) == 22);
  assert(find_label(grid, "backup") && find_label(grid, "nested") && find_label(grid, "usb"));
  assert(!find_label(grid, "usb_0"));
  while (!requests.empty()) {
    assert(requests.front().params["filename"].get<std::string>().compare(0, 4, "usb/") != 0);
    reply("server.files.metadata", metadata());
  }
  lv_obj_scroll_to_y(grid, LV_COORD_MAX, LV_ANIM_OFF);
  const int printer_scroll = lv_obj_get_scroll_y(grid);
  lv_event_send(grid, LV_EVENT_SCROLL_END, nullptr);
  auto old_preview = take("server.files.metadata");
  select_page(file_tabs(), 1);
  assert(gallery() == grid);
  assert(lv_obj_get_scroll_y(grid) == 0);
  assert(lv_obj_get_child_cnt(grid) == 20);
  assert(!find_label(grid, "part_0") && !find_label(grid, "backup"));
  assert(lv_obj_get_parent(label("usb_0")) == lv_obj_get_child(grid, 0));
  assert(lv_obj_get_parent(label("usb_1")) == lv_obj_get_child(grid, 1));
  auto stale_data = metadata(999);
  stale_data["result"]["filament_type"] = "STALE";
  old_preview.callback(stale_data);
  assert(requests.size() == 1);
  while (!requests.empty()) {
    assert(requests.front().params["filename"].get<std::string>().compare(0, 4, "usb/") == 0);
    reply("server.files.metadata", metadata());
  }
  assert(!find_label(grid, "STALE"));
  lv_obj_scroll_to_y(grid, LV_COORD_MAX, LV_ANIM_OFF);
  const int usb_scroll = lv_obj_get_scroll_y(grid);
  select_page(file_tabs(), 0);
  assert(lv_obj_get_scroll_y(grid) == printer_scroll);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  // Switching source during a file refresh must not invalidate that refresh.
  panel.handle_file_list_change(changed);
  select_page(file_tabs(), 1);
  reply_files(listing);
  assert(lv_obj_get_scroll_y(grid) == usb_scroll);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  assert(!lv_obj_is_visible(find_spinner(panel_root)));

  // A late directory reply cannot repopulate a closed or reopened browser.
  panel.handle_file_list_change(changed);
  reply("server.files.list", listing);
  auto directory = take("server.files.get_directory");
  panel.background();
  panel.foreground();
  json old_directory = {{"result", {{"dirs", json::array({{{"dirname", "usb"}}})}}}};
  directory.callback(old_directory);
  assert(lv_obj_get_child_cnt(grid) == 0);
  reply_files(file_list(1));
  assert(tab_count() == 1);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }

  // If the directory query fails, listed USB files must still be reachable.
  panel.handle_file_list_change(changed);
  reply("server.files.list", listing);
  reply("server.files.get_directory", {{"error", {{"message", "Directory unavailable"}}}});
  assert(tab_count() == 2);
  select_page(file_tabs(), 1);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  assert(find_label(gallery(), "usb_0"));
  panel.background();
  panel.foreground();
}

static void test_usb_metadata_scan(PrintPanel &panel) {
  char directory[] = "/tmp/grumpy-usb-preview-XXXXXX";
  assert(mkdtemp(directory));
  const std::string root = std::string(directory) + "/gcodes";
  const std::string drive = std::string(directory) + "/drive";
  assert(mkdir(root.c_str(), 0700) == 0);
  assert(mkdir(drive.c_str(), 0700) == 0);
  assert(mkdir((drive + "/models").c_str(), 0700) == 0);
  assert(symlink(drive.c_str(), (root + "/usb").c_str()) == 0);
  unsigned char rgba[] = {40, 180, 100, 255};
  unsigned char *png = nullptr;
  size_t size = 0;
  assert(lodepng_encode32(&png, &size, rgba, 1, 1) == 0);
  {
    std::ofstream image_file(drive + "/models/preview.png", std::ios::binary);
    image_file.write(reinterpret_cast<const char *>(png), size);
  }
  lv_mem_free(png);
  json roots = json::array({{{"name", "gcodes"}, {"path", root}}});
  State::get_instance()->set_data("roots", roots, "");
  const std::string path = "usb/models/usb_preview.gcode";
  const json listing = {{"result", json::array({{{"path", path}, {"modified", 1000}, {"size", 1024}}})}};
  const json missing = {{"error", {{"code", -32601}, {"message", "Metadata not available for <" + path + ">"}}}};
  auto preview = metadata();
  preview["result"]["thumbnails"] = json::array({{{"width", 1}, {"height", 1}, {"relative_path", "preview.png"}}});
  reply_files(listing);
  assert(requests.empty()); // no USB scans while browsing Printer
  select_page(file_tabs(), 1);
  assert(requests.front().params == json({{"filename", path}}));
  reply("server.files.metadata", missing);
  assert(requests.size() == 1 && requests.front().method == "server.files.metascan");
  assert(requests.front().params == json({{"filename", path}}));
  auto *card = lv_obj_get_parent(label("usb_preview"));
  assert(find_spinner(card));
  click("usb_preview", LV_EVENT_SHORT_CLICKED);
  assert(lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  reply("server.files.metascan", preview);
  assert(requests.empty() && !find_spinner(card));
  assert(find_label(card, "2m3s") && find_label(card, "4.5 g"));
  assert(!lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  auto *image = lv_obj_get_child(lv_obj_get_child(card, 0), 0);
  const std::string image_path = "A:" + root + "/usb/models/preview.png";
  assert(std::strcmp(static_cast<const char *>(lv_img_get_src(image)), image_path.c_str()) == 0);
  auto *decoded = _lv_img_cache_open(lv_img_get_src(image), lv_color_black(), 0);
  assert(decoded && decoded->dec_dsc.img_data); // decode through the USB symlink
  click("Back");
  select_page(file_tabs(), 0);
  select_page(file_tabs(), 1);
  assert(requests.empty()); // switching sources reuses the scanned metadata

  panel.background();
  panel.foreground();
  reply_files(listing);
  select_page(file_tabs(), 1);
  reply("server.files.metadata", missing);
  reply("server.files.metascan", missing);
  assert(requests.empty()); // a failed scan must not recurse
  card = lv_obj_get_parent(label("usb_preview"));
  assert(!find_spinner(card) && find_label(card, "Unavailable"));

  // Upstream Moonraker uses 404 for the same missing-metadata response.
  click("usb_preview", LV_EVENT_SHORT_CLICKED);
  auto not_found = missing;
  not_found["error"]["code"] = 404;
  reply("server.files.metadata", not_found);
  auto stale_scan = take("server.files.metascan");
  json changed = {{"params", json::array({{{"item", {{"root", "gcodes"}}}}})}};
  panel.handle_file_list_change(changed);
  reply_files({{"result", json::array()}}); // unplug while the scan is pending
  stale_scan.callback(preview);
  assert(requests.empty());
  assert(find_label(gallery(), "No G-code files"));
  assert(!find_action(lv_layer_top(), "Print"));

  panel.handle_file_list_change(changed);
  reply_files(listing);
  select_page(file_tabs(), 1);
  auto stale_metadata = take("server.files.metadata");
  select_page(file_tabs(), 0);
  auto missing_data = missing;
  stale_metadata.callback(missing_data);
  assert(requests.empty()); // do not scan a source that is no longer displayed
  panel.background();
  panel.foreground();
  assert(unlink((root + "/usb").c_str()) == 0);
  std::remove((drive + "/models/preview.png").c_str());
  rmdir((drive + "/models").c_str());
  rmdir(drive.c_str());
  rmdir(root.c_str());
  rmdir(directory);
}

static void stress_preview_scrolling(const json &fixture) {
  auto *grid = gallery();
  auto *pointer = create_pointer();
  int frames = 0;
  auto frame = [&] {
    usleep(5000); // use real ticks, as the SDL build does
    lv_timer_handler();
    lv_refr_now(nullptr);
    if (++frames % 4 == 0 && !requests.empty()) {
      const auto path = requests.front().params["filename"].get<std::string>();
      reply("server.files.metadata", {{"result", fixture["metadata"][path]}});
    }
  };
  for (int swipe = 0; swipe < 40; ++swipe) {
    const int x = grid->coords.x2 - (swipe % 4);
    const bool upwards = swipe % 4 < 2;
    const int start = grid->coords.y1 + lv_obj_get_height(grid) * (upwards ? 3 : 1) / 4;
    const int direction = upwards ? -1 : 1;
    pointer_event(pointer, x, start, LV_INDEV_STATE_PRESSED);
    for (int step = 1; step <= 8; ++step) {
      pointer_event(pointer, x + step % 3, start + direction * step * lv_obj_get_height(grid) / 12,
          LV_INDEV_STATE_PRESSED);
      frame();
    }
    pointer_event(pointer, x + 2, start + direction * 8 * lv_obj_get_height(grid) / 12,
        LV_INDEV_STATE_RELEASED);
    for (int settle = 0; settle < 20; ++settle) {
      frame(); // exercise momentum, label animations and actual PNG decoding
    }
  }
  lv_indev_delete(pointer);
  std::puts("edge scrolling with momentum and thumbnail rendering passed");
}

static lv_obj_t *find_chart(lv_obj_t *parent) {
  if (lv_obj_check_type(parent, &lv_chart_class)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
    if (auto *chart = find_chart(lv_obj_get_child(parent, i))) {
      return chart;
    }
  }
  return nullptr;
}

static void test_home_chart(MainPanel &main, lv_obj_t *home) {
  auto *chart = find_chart(home);
  assert(chart);
  auto *box = lv_obj_get_parent(chart);
  auto *page = lv_obj_get_parent(box);
  assert(lv_obj_get_parent(page) == home);
  lv_obj_update_layout(home);
  auto *homing = lv_obj_get_parent(find_label(home, "Homing"));
  auto *extruder = lv_obj_get_parent(find_label(home, "Extruder"));
  auto *fans = lv_obj_get_parent(find_label(home, "Fans"));
  auto *led = lv_obj_get_parent(find_label(home, "LED"));
  assert(homing->coords.y1 == page->coords.y1 + Theme::gap());
  assert(homing->coords.x1 == extruder->coords.x1);
  assert(fans->coords.x1 == extruder->coords.x1 && led->coords.x1 == extruder->coords.x1);
  assert(extruder->coords.y1 == homing->coords.y2 + 1 + Theme::gap());
  assert(fans->coords.y1 == extruder->coords.y2 + 1 + Theme::gap());
  assert(led->coords.y1 == fans->coords.y2 + 1 + Theme::gap());
  assert(box->coords.x1 == page->coords.x1 + Theme::gap());
  assert(box->coords.x2 < homing->coords.x1);
  assert(box->coords.y2 == led->coords.y2);
  for (auto *button : {homing, extruder, fans, led}) {
    assert(lv_obj_get_height(button) >= Theme::touch_h());
    assert(lv_obj_get_height(lv_obj_get_child(button, 0)) <= Theme::action_icon_size());
  }
  assert(lv_obj_get_height(box) >= Theme::scale_h(60));
  const auto bounds = box->coords;

  // Reconfiguring or scrolling sensor rows must leave the graph fixed below them.
  for (int count : {3, 4, 8, 0, 3}) {
    json sensors = json::object();
    for (int i = 0; i < count; ++i) {
      sensors["extruder" + std::to_string(i)] = {
        {"display_name", "Tool " + std::to_string(i)}, {"controllable", true}, {"color", "red"}};
    }
    main.create_sensors(sensors);
    lv_obj_update_layout(home);
    int series_count = 0;
    for (auto *series = lv_chart_get_series_next(chart, nullptr); series != nullptr;
         series = lv_chart_get_series_next(chart, series)) {
      ++series_count;
    }
    assert(series_count == count);
    if (count == 3) {
      auto *first = lv_obj_get_parent(find_label(home, "Tool 0"));
      auto *last = lv_obj_get_parent(find_label(home, "Tool 2"));
      auto *readouts = lv_obj_get_parent(first);
      assert(readouts->coords.x1 == box->coords.x1 && readouts->coords.x2 == box->coords.x2);
      assert(first->coords.y1 >= readouts->coords.y1);
      assert(last->coords.y2 <= readouts->coords.y2);
      assert(!lv_obj_has_flag(readouts, LV_OBJ_FLAG_SCROLLABLE));
      // Showing the existing print chip does not add scrolling for three readings.
      auto *mini = lv_obj_get_child(readouts, 0);
      lv_obj_clear_flag(mini, LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(home);
      assert(!lv_obj_has_flag(readouts, LV_OBJ_FLAG_SCROLLABLE));
      lv_obj_add_flag(mini, LV_OBJ_FLAG_HIDDEN);
      lv_obj_update_layout(home);
    }
    if (count > 3) {
      auto *readouts = lv_obj_get_parent(lv_obj_get_parent(find_label(home, "Tool 0")));
      assert(lv_obj_has_flag(readouts, LV_OBJ_FLAG_SCROLLABLE));
      assert(lv_obj_get_scroll_bottom(readouts) > 0);
      lv_obj_scroll_to_y(readouts, LV_COORD_MAX, LV_ANIM_OFF);
      lv_obj_update_layout(home);
      assert(lv_obj_get_scroll_y(readouts) > 0);
    }
    assert(box->coords.x1 == bounds.x1 && box->coords.x2 == bounds.x2);
    assert(box->coords.y1 == bounds.y1 && box->coords.y2 == bounds.y2);
  }

  // Keep the temperature keypad usable beside the narrow action column.
  auto *sensor = lv_obj_get_parent(find_label(home, "Tool 0"));
  lv_event_send(sensor, LV_EVENT_CLICKED, nullptr);
  auto *keypad = lv_obj_get_child(page, -1);
  lv_obj_update_layout(home);
  assert(lv_obj_check_type(lv_obj_get_child(keypad, 1), &lv_keyboard_class));
  assert(lv_obj_get_width(keypad) >= lv_obj_get_content_width(page) / 2);
  lv_event_send(lv_obj_get_child(keypad, 0), LV_EVENT_CANCEL, nullptr);
}

static void test_home_actions(MainPanel &main, lv_obj_t *tabs) {
  auto *pages = lv_tabview_get_content(tabs);
  auto *home = lv_obj_get_child(pages, 0);
  auto *stop_label = find_label(tabs, "Stop");
  assert(stop_label);
  auto *stop = lv_obj_get_parent(stop_label);
  lv_obj_update_layout(lv_scr_act());
  assert(lv_obj_get_parent(stop) == tabs);
  assert(stop->coords.x1 == 0);
  assert(!find_label(home, "Stop"));
  auto *buttons = lv_tabview_get_tab_btns(tabs);
  auto *matrix = reinterpret_cast<lv_btnmatrix_t *>(buttons);
  assert(buttons->coords.y1 + matrix->button_areas[matrix->btn_cnt - 1].y2 < stop->coords.y1);
  auto *stop_icon = lv_obj_get_child(stop, 0);
  const int icon_h = lv_obj_get_height(stop_icon);
  auto icon_top = [&](int index) {
    const auto &area = matrix->button_areas[index];
    return buttons->coords.y1 + area.y1 + (lv_area_get_height(&area) - icon_h) / 2;
  };
  const int last_top = icon_top(matrix->btn_cnt - 1);
  const int nav_gap = last_top - icon_top(matrix->btn_cnt - 2) - icon_h;
  const int above_stop = stop_icon->coords.y1 - last_top - icon_h;
  const int below_stop = screen_height - 1 - stop_icon->coords.y2;
  assert(nav_gap > 0);
  // Integer image zoom and row division can differ by a pixel or two.
  assert(std::abs(above_stop - nav_gap) <= 2);
  assert(std::abs(below_stop - nav_gap) <= 2);

  json fans = {{"fan", {{"display_name", "Part cooling"}}}};
  main.create_fans(fans);
  json leds = json::array({{{"id", "output_pin light"}, {"display_name", "Light"}, {"pwm", true}}});
  main.create_leds(leds);
  test_home_chart(main, home);
  auto *home_icon = lv_obj_get_child(lv_obj_get_parent(find_label(home, "Homing")), 0);
  auto same_icon_size = [&](lv_obj_t *panel, const char *text) {
    auto *caption = find_label(panel, text);
    assert(caption);
    auto *icon = lv_obj_get_child(lv_obj_get_parent(caption), 0);
    assert(lv_obj_check_type(icon, &lv_img_class));
    assert(std::abs(lv_obj_get_width(icon) - lv_obj_get_width(home_icon)) <= 1);
    assert(std::abs(lv_obj_get_height(icon) - lv_obj_get_height(home_icon)) <= 1);
  };
  const auto console_index = lv_obj_get_child_cnt(pages) - 2;
  for (const auto &action : {std::make_pair("Homing", "Home All"),
                             std::make_pair("Extruder", "Load"),
                             std::make_pair("Fans", "All On"),
                             std::make_pair("LED", "Light")}) {
    auto open = [&] {
      lv_event_send(lv_obj_get_parent(find_label(home, action.first)), LV_EVENT_CLICKED, nullptr);
      auto *panel = lv_obj_get_child(lv_scr_act(), -1);
      lv_obj_update_layout(lv_scr_act());
      assert(find_label(panel, action.second));
      assert(!find_label(panel, "Back") && !find_label(panel, "Stop"));
      assert(panel->coords.x1 == stop->coords.x2 + 1);
      assert(panel->coords.x2 == screen_width - 1);
      assert(panel->coords.y1 == 0 && panel->coords.y2 == screen_height - 1);
      if (std::strcmp(action.first, "Homing") == 0) {
        for (const char *text : {"Home All", "Home XY", "Motors Off", "X-", "Y+", "Z+"}) {
          same_icon_size(panel, text);
        }
      } else if (std::strcmp(action.first, "Extruder") == 0) {
        auto *tabs = lv_obj_get_child(panel, 0);
        for (int index : {0, 1}) {
          select_page(tabs, index);
          auto *tab = lv_obj_get_child(lv_tabview_get_content(tabs), index);
          auto *cooldown = lv_obj_get_parent(find_label(tab, "Cooldown"));
          same_icon_size(tab, "Cooldown");
          same_icon_size(tab, index == 0 ? "Load" : "Extrude");
          same_icon_size(tab, index == 0 ? "Unload" : "Retract");
          assert(cooldown->coords.y2 == tab->coords.y2 - Theme::gap());
          assert(lv_obj_get_height(cooldown) == Theme::action_tile_h());
        }
      } else if (std::strcmp(action.first, "Fans") == 0 || std::strcmp(action.first, "LED") == 0) {
        auto *on = lv_obj_get_parent(find_label(panel, "All On"));
        auto *off = lv_obj_get_parent(find_label(panel, "All Off"));
        same_icon_size(panel, "All On");
        same_icon_size(panel, "All Off");
        same_icon_size(panel, "Off");
        same_icon_size(panel, "Max");
        assert(off->coords.y2 == panel->coords.y2 - Theme::gap());
        assert(off->coords.y1 == on->coords.y2 + 1 + Theme::gap());
        assert(lv_obj_get_height(on) == Theme::action_tile_h());
        assert(lv_obj_get_height(off) == Theme::action_tile_h());
      }
      return panel;
    };
    auto *panel = open();
    assert(main.home_active());
    select_page(tabs, 0); // Reselect Home through real pointer input.
    assert(lv_obj_get_index(panel) < lv_obj_get_index(tabs));
    assert(main.home_active());
    panel = open();
    select_page(tabs, console_index);
    assert(lv_obj_get_index(panel) < lv_obj_get_index(tabs));
    select_page(tabs, 0);
  }

  // The nav E-stop is reachable while a home action covers the page.
  lv_event_send(lv_obj_get_parent(find_label(home, "Homing")), LV_EVENT_CLICKED, nullptr);
  auto *pointer = create_pointer();
  const int x = (stop->coords.x1 + stop->coords.x2) / 2;
  const int y = (stop->coords.y1 + stop->coords.y2) / 2;
  pointer_event(pointer, x, y, LV_INDEV_STATE_PRESSED);
  assert(pointer->proc.types.pointer.act_obj == stop);
  pointer_event(pointer, x, y, LV_INDEV_STATE_RELEASED);
  lv_indev_delete(pointer);
  assert(requests.empty()); // A normal tap requires confirmation.
  auto *dialog = lv_obj_get_parent(find_label(lv_scr_act(), "Emergency Stop"));
  choose(dialog, 0);
  assert(requests.empty());
  lv_event_send(stop, LV_EVENT_CLICKED, nullptr);
  dialog = lv_obj_get_parent(find_label(lv_scr_act(), "Emergency Stop"));
  choose(dialog, 1);
  take("printer.emergency_stop");
  assert(requests.empty());

  const std::string config_path = "/tmp/grumpy-navigation-" + std::to_string(getpid()) + ".cfg";
  {
    std::ofstream config(config_path);
    config << "[ui]\nprompt_emergency_stop: false\n";
  }
  assert(Config::get_instance()->load_override(config_path));
  lv_event_send(stop, LV_EVENT_CLICKED, nullptr);
  assert(requests.empty()); // With confirmation off, only a long press fires.
  lv_event_send(stop, LV_EVENT_LONG_PRESSED, nullptr);
  take("printer.emergency_stop");
  lv_event_send(stop, LV_EVENT_CLICKED, nullptr);
  assert(requests.empty());
  assert(Config::get_instance()->load("grumpyscreen.cfg"));
  std::remove(config_path.c_str());
  select_page(tabs, 0);
  main.set_klipper_ready(false);
  assert(lv_obj_has_state(stop, LV_STATE_DISABLED));
  main.set_klipper_ready(true);
  assert(!lv_obj_has_state(stop, LV_STATE_DISABLED));
}

static void test_navigation(KWebSocketClient &ws, std::mutex &lock) {
  auto &main = create_navigation(ws, lock);
  auto *tabs = navigation();
  test_home_actions(main, tabs);
  auto *buttons = lv_tabview_get_tab_btns(tabs);
  auto *pages = lv_tabview_get_content(tabs);
  const auto print_index = lv_obj_get_child_cnt(pages) - 3;
  auto *page = lv_obj_get_child(pages, print_index);
  panel_root = lv_obj_get_child(page, 0);
  assert(lv_obj_has_flag(panel_root, LV_OBJ_FLAG_HIDDEN));
  assert(!find_label(tabs, "Print")); // printing is a nav icon, not a Home action
  select_page(tabs, print_index);
  assert(!lv_obj_has_flag(panel_root, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_is_visible(buttons));
  assert(requests.size() == 1 && requests.front().method == "server.files.list");
  select_page(tabs, print_index);
  assert(requests.size() == 1); // reselecting does not reload
  reply_files(file_list());
  auto *grid = lv_obj_get_parent(lv_obj_get_parent(label("part_0")));
  const int columns = screen_width == 480 ? 3 : 4;
  auto *row_start = lv_obj_get_parent(label("part_0"));
  auto *row_end = lv_obj_get_parent(label(("part_" + std::to_string(columns - 1)).c_str()));
  auto *next_row = lv_obj_get_parent(label(("part_" + std::to_string(columns)).c_str()));
  assert(lv_obj_get_y(row_start) == lv_obj_get_y(row_end));
  assert(lv_obj_get_y(next_row) > lv_obj_get_y(row_start));
  assert(!find_label(panel_root, "Back"));
  assert(lv_obj_get_scrollbar_mode(grid) == LV_SCROLLBAR_MODE_OFF);
  // Swiping directly on a file scrolls the gallery without opening its modal.
  lv_obj_update_layout(tabs);
  const int swipe_x = (row_end->coords.x1 + row_end->coords.x2) / 2;
  const int swipe_y = (row_end->coords.y1 + row_end->coords.y2) / 2;
  auto *pointer = create_pointer();
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  pointer_event(pointer, swipe_x, swipe_y, LV_INDEV_STATE_PRESSED);
  assert(pointer->proc.types.pointer.act_obj == row_end);
  pointer_event(pointer, swipe_x, swipe_y - Theme::scale_r(50), LV_INDEV_STATE_PRESSED);
  pointer_event(pointer, swipe_x, swipe_y - Theme::scale_r(50), LV_INDEV_STATE_RELEASED);
  assert(lv_obj_get_scroll_y(grid) > 0);
  assert(!find_action(lv_layer_top(), "Print"));
  lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_indev_delete(pointer);
  auto stale = take("server.files.metadata");
  select_page(tabs, 0);
  assert(lv_obj_has_flag(panel_root, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_get_child_cnt(grid) == 0); // leaving the tab releases its cards
  auto details = metadata();
  stale.callback(details);
  assert(requests.empty());

  select_page(tabs, print_index);
  reply_files(file_list(1));
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  click("part_0", LV_EVENT_SHORT_CLICKED);
  click("Print");
  assert(take("printer.print.start").params["filename"] == "part_0.gcode");
  assert(main.home_active()); // starting a print leaves the file browser
  auto *status_panel = lv_obj_get_child(lv_scr_act(), -1);
  lv_obj_update_layout(lv_scr_act());
  assert(find_label(status_panel, "Fine Tune"));
  assert(!find_label(status_panel, "Stop"));
  assert(status_panel->coords.x1 == lv_obj_get_width(buttons));
  select_page(tabs, 0);
  assert(lv_obj_get_index(status_panel) < lv_obj_get_index(tabs));
  assert(lv_obj_has_flag(panel_root, LV_OBJ_FLAG_HIDDEN));
  main.set_klipper_ready(false); // close the print-status overlay
  main.set_klipper_ready(true);
  select_page(tabs, print_index);
  stale = take("server.files.list");
  printer_state("printing");
  json update = {{"params", json::array({{{"print_stats", {{"state", "printing"}}}}})}};
  main.consume(update);
  assert(main.home_active());
  assert(lv_btnmatrix_has_btn_ctrl(buttons, print_index, LV_BTNMATRIX_CTRL_DISABLED));
  select_page(tabs, print_index);
  assert(main.home_active()); // the disabled Print item ignores pointer input
  auto listing = file_list();
  stale.callback(listing);
  assert(lv_obj_get_child_cnt(grid) == 0 && requests.empty());
  printer_state("standby");
  main.set_klipper_ready(false);
  lv_obj_move_background(tabs);
  panel_root = nullptr;
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
  printer_state("standby");

  // Optional fixture from the simulator for inspecting the actual rendered UI.
  if (const char *fixture = std::getenv("PRINT_PANEL_FIXTURE")) {
    create_navigation(ws, lock);
    auto *tabs = navigation();
    auto *pages = lv_tabview_get_content(tabs);
    const auto print_index = lv_obj_get_child_cnt(pages) - 3;
    select_page(tabs, print_index);
    panel_root = lv_obj_get_child(lv_obj_get_child(pages, print_index), 0);
    json data;
    std::ifstream(fixture) >> data;
    json roots = json::array({{{"name", "gcodes"}, {"path", data["root"]}}});
    State::get_instance()->set_data("roots", roots, "");
    reply_files({{"result", data["files"]}}, access((data["root"].get<std::string>() + "/usb").c_str(), F_OK) == 0);
    if (std::getenv("PRINT_PANEL_USB") != nullptr) {
      select_page(file_tabs(), 1);
    }
    const char *limit_setting = std::getenv("PRINT_PANEL_METADATA_LIMIT");
    const int limit = limit_setting == nullptr ? -1 : std::atoi(limit_setting);
    int loaded = 0;
    while (!requests.empty() && (limit < 0 || loaded < limit)) {
      const auto path = requests.front().params["filename"].get<std::string>();
      reply("server.files.metadata", {{"result", data["metadata"][path]}});
      ++loaded;
    }
    if (std::getenv("PRINT_PANEL_STRESS") != nullptr) {
      stress_preview_scrolling(data);
    }
    if (const char *selected = std::getenv("PRINT_PANEL_FILE")) {
      click(selected, LV_EVENT_SHORT_CLICKED);
    }
    if (std::getenv("PRINT_PANEL_HOME") != nullptr) {
      select_page(tabs, 0);
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

  test_navigation(ws, lock);
  auto &status = *new PrintStatusPanel(ws, lock, lv_scr_act());
  {
    auto *parent = lv_obj_create(lv_scr_act());
    PrintPanel embedded(ws, lock, status, nullptr, parent);
    embedded.foreground();
    auto stale = take("server.files.list");
    lv_obj_del(parent); // tabview teardown owns the embedded gallery's widgets
    assert(embedded.get_container() == nullptr);
    auto listing = file_list();
    stale.callback(listing);
    assert(requests.empty());
  }
  PrintPanel panel(ws, lock, status, nullptr);
  lv_obj_update_layout(lv_scr_act());
  panel_root = panel.get_container();
  requests.clear();
  panel.foreground();
  test_tile_captions(panel, status);
  test_preview_cache(panel);
  test_source_tabs(panel);
  test_usb_metadata_scan(panel);
  reply_files(file_list());
  auto *grid = lv_obj_get_parent(lv_obj_get_parent(label("part_0")));
  auto *first = lv_obj_get_parent(label("part_0"));
  auto *second = lv_obj_get_parent(label("part_1"));
  const int columns = screen_width == 480 ? 3 : 4;
  auto *row_end = lv_obj_get_parent(label(("part_" + std::to_string(columns - 1)).c_str()));
  auto *next_row = lv_obj_get_parent(label(("part_" + std::to_string(columns)).c_str()));
  lv_obj_update_layout(grid);
  assert(lv_obj_get_height(label("part_0")) == lv_font_get_line_height(Theme::scale_font(12)) + Theme::scale_r(4));
  assert(lv_obj_get_x(first) < lv_obj_get_x(second));
  assert(lv_obj_get_y(first) == lv_obj_get_y(second));
  assert(lv_obj_get_y(row_end) == lv_obj_get_y(first));
  assert(lv_obj_get_y(next_row) > lv_obj_get_y(first));
  // Nested files are cards in the same gallery; folders never take a slot.
  assert(lv_obj_get_child_cnt(grid) == 19);
  assert(!find_label(grid, "parts"));
  assert(find_label(grid, "a \"b\""));
  lv_area_t grid_bounds;
  lv_obj_get_coords(grid, &grid_bounds);
  assert(!find_label(panel_root, "Back"));
  assert(grid_bounds.x2 >= screen_width - Theme::gap() - 1);
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
  unknown["result"]["filament_type"] = nullptr;
  reply("server.files.metadata", unknown);
  assert(!find_spinner(first));
  assert(find_label(first, "Unknown"));
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
  assert(find_label(grid, "PLA"));
  assert(find_label(grid, "2m3s"));
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
  reply_files(file_list());
  assert(requests.empty());
  assert(lv_obj_get_scroll_y(grid) == saved_scroll);

  lv_obj_scroll_to_y(grid, saved_scroll / 2, LV_ANIM_OFF);
  lv_event_send(grid, LV_EVENT_SCROLL_END, nullptr);

  // A USB insertion adds a separate source while an old preview is pending.
  auto stale = take("server.files.metadata");
  lv_obj_scroll_to_y(grid, 0, LV_ANIM_OFF);
  panel.handle_file_list_change(changed);
  auto with_usb = file_list();
  with_usb["result"].push_back({{"path", "usb/models/a \"b\".gcode"}, {"modified", 2000}});
  reply_files(with_usb);
  assert(lv_obj_get_child_cnt(grid) == 19);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  select_page(file_tabs(), 1);
  auto stale_data = metadata(999);
  stale_data["result"]["filament_type"] = "STALE";
  stale.callback(stale_data);
  assert(requests.size() == 1); // stale replies do not start a duplicate request
  assert(requests.front().params["filename"] == "usb/models/a \"b\".gcode");
  reply("server.files.metadata", metadata(42));
  assert(!find_label(grid, "STALE"));
  assert(lv_obj_get_child_cnt(grid) == 1);
  assert(!find_label(grid, "USB"));
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  // The USB and printer files share a basename but retain their full paths.
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  assert(find_label(lv_layer_top(), "usb/models/a \"b\".gcode"));
  assert(lv_obj_is_visible(find_label(panel_root, "a \"b\""))); // the gallery remains behind the modal
  auto *modal = lv_obj_get_parent(lv_obj_get_parent(label("Print")));
  lv_obj_update_layout(modal);
  assert(lv_obj_get_width(modal) < screen_width);
  assert(lv_obj_get_height(modal) < screen_height);
  assert(!lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  click("Print");
  assert(take("printer.print.start").params["filename"] == "usb/models/a \"b\".gcode");
  status.background();
  panel.foreground();
  reply_files(with_usb);
  select_page(file_tabs(), 1);
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
    assert(lv_obj_has_flag(panel_root, LV_OBJ_FLAG_HIDDEN));
    assert(requests.empty());
  }
  printer_state("standby");
  panel.foreground();
  reply_files(with_usb);
  select_page(file_tabs(), 1);
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
  assert(requests.front().params["path"] == "gcodes/usb/models/a \"b\".gcode");
  reply("server.files.delete_file", {{"result", {{"action", "delete_file"}}}});
  reply_files({{"result", json::array()}}, true);
  assert(lv_obj_get_child_cnt(lv_tabview_get_content(file_tabs())) == 2);
  assert(find_label(lv_scr_act(), "No G-code files"));
  assert(!find_action(lv_layer_top(), "Print"));

  // Unplugging USB while its selected file is loading returns to the gallery.
  panel.handle_file_list_change(changed);
  reply_files({{"result", json::array({with_usb["result"].back()})}});
  click("a \"b\"", LV_EVENT_SHORT_CLICKED);
  assert(lv_obj_has_state(lv_obj_get_parent(label("Print")), LV_STATE_DISABLED));
  stale = take("server.files.metadata");
  panel.handle_file_list_change(changed);
  reply_files(file_list(1));
  stale_data = metadata(999);
  stale.callback(stale_data);
  while (!requests.empty()) {
    reply("server.files.metadata", metadata());
  }
  assert(lv_obj_get_child_cnt(grid) == 2);
  assert(!find_label(lv_layer_top(), "usb/models/a \"b\".gcode", true));
  assert(!find_action(lv_layer_top(), "Print"));
  panel.background();
  panel.foreground();
  reply_files({{"error", {{"message", "Test list failure"}}}});
  assert(find_label(lv_layer_top(), "File list failed"));
  std::puts("print card layout, Printer/USB tabs, lazy metadata, print and delete tests passed");
}
