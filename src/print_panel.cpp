#include "print_panel.h"
#include "state.h"
#include "utils.h"
#include "logger.h"
#include "icons.h"
#include "simple_dialog.h"
#include "theme.h"

#include <algorithm>
#include <cstring>
#include <regex>

using namespace Theme;

namespace {

bool usb_file(const std::string &path) {
  return path.compare(0, 4, "usb/") == 0;
}

std::pair<std::string, std::string> tile_caption(const std::string &filename) {
  std::string stem = filename;
  const auto extension = stem.find_last_of('.');
  if (extension != std::string::npos && extension > 0) {
    stem.resize(extension);
  }
  // Only shorten a complete model_MATERIAL_duration suffix. Model names may
  // themselves contain underscores; material variants may contain + or -.
  static const std::regex suffix(R"(^(.+)_([A-Z][A-Za-z0-9+ .-]*)_((?:[0-9]+d)?(?:[0-9]+h)?(?:[0-9]+m)?(?:[0-9]+s)?)$)");
  std::smatch match;
  if (std::regex_match(stem, match, suffix) && match[3].length() > 0) {
    return {match[1].str(), match[2].str()};
  }
  return {stem, ""};
}

std::string metadata_material(const json &metadata) {
  json types = metadata.value("filament_type", json());
  if (types.is_string()) {
    const auto text = types.get<std::string>();
    if (!text.empty() && text.front() == '[') {
      auto parsed = json::parse(text, nullptr, false);
      if (parsed.is_array()) {
        types = std::move(parsed);
      }
    }
  }
  if (types.is_string()) {
    types = KUtils::split(types.get<std::string>(), ';');
  }
  std::vector<std::string> materials;
  if (types.is_array()) {
    for (const auto &type : types) {
      if (!type.is_string()) {
        continue;
      }
      auto material = type.get<std::string>();
      material.erase(0, material.find_first_not_of(" \t\""));
      material.erase(material.find_last_not_of(" \t\"") + 1);
      if (!material.empty() && std::find(materials.begin(), materials.end(), material) == materials.end()) {
        materials.push_back(std::move(material));
      }
    }
  }
  return join(materials, "/");
}

std::string tile_time(int64_t seconds) {
  if (seconds >= 86400) {
    return fmt::format("{}d{}h", seconds / 86400, seconds / 3600 % 24);
  }
  if (seconds >= 3600) {
    return fmt::format("{}h{}m", seconds / 3600, seconds / 60 % 60);
  }
  if (seconds >= 60) {
    return fmt::format("{}m{}s", seconds / 60, seconds % 60);
  }
  return seconds > 0 ? fmt::format("{}s", seconds) : "Time ?";
}

} // namespace

PrintPanel::PrintPanel(KWebSocketClient &websocket, std::mutex &lock, PrintStatusPanel &ps, MmuPanel *m,
                       lv_obj_t *parent)
  : NotifyConsumer(lock)
  , ws(websocket)
  , files_cont(create_screen(parent))
  , spinner(lv_spinner_create(files_cont, 1000, 60))
  , browser_view(create_row(files_cont))
  , file_view(lv_obj_create(lv_layer_top()))
  , file_dialog(lv_obj_create(file_view))
  , close_btn(file_dialog, Icons::BACK, "Back", &PrintPanel::_handle_back_btn, this)
  , print_btn(file_dialog, Icons::PRINT, "Print", &PrintPanel::_handle_print_callback, this)
  , delete_btn(file_dialog, Icons::DELETE_IMG, "Delete", [](lv_event_t *e) {
      static_cast<PrintPanel *>(lv_event_get_user_data(e))->confirm_delete();
    }, this)
  , cur_file(NULL)
  , file_panel(file_dialog)
  , print_status(ps)
  , mmu(m)
  , refreshing_files(false)
  , refresh_pending(false)
  , visible(false)
{
  lv_obj_add_flag(files_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(files_cont, [](lv_event_t *e) {
    auto *panel = static_cast<PrintPanel *>(lv_event_get_user_data(e));
    panel->visible = false;
    panel->files_cont = NULL;
    panel->file_grid = NULL;
    panel->scroll_extent = NULL;
  }, LV_EVENT_DELETE, this);

  lv_obj_set_size(browser_view, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(browser_view, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(files_cont, 0, 0);
  update_source_tabs(false);

  file_grid = create_row(browser_view);
  lv_obj_set_size(file_grid, LV_PCT(100), 0);
  lv_obj_set_flex_grow(file_grid, 1);
  lv_obj_set_flex_flow(file_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(file_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  manage_scroll(file_grid);
  lv_obj_add_event_cb(file_grid, [](lv_event_t *e) {
    auto *panel = static_cast<PrintPanel *>(lv_event_get_user_data(e));
    panel->update_visible_tiles();
    panel->load_visible_metadata();
  }, LV_EVENT_SCROLL, this);
  lv_obj_add_event_cb(file_grid, [](lv_event_t *e) {
    static_cast<PrintPanel *>(lv_event_get_user_data(e))->load_visible_metadata();
  }, LV_EVENT_SCROLL_END, this);

  // File details sit over the gallery, which retains its scroll position.
  lv_obj_remove_style_all(file_view);
  lv_obj_add_style(file_view, &styles().popout, 0);
  lv_obj_set_size(file_view, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(file_view, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(file_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(file_view, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_style(file_dialog, &styles().popout_box, 0);
  lv_obj_set_style_text_font(file_dialog, scale_font(16), 0);
  lv_obj_clear_flag(file_dialog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(file_dialog, std::min(popout_w(), scale_w(360)), std::min(popout_max_h(), scale_h(244)));
  lv_obj_center(file_dialog);
  static lv_coord_t rows[] = {LV_GRID_FR(1), 0, LV_GRID_TEMPLATE_LAST};
  rows[1] = scale_r(64);
  static lv_coord_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(file_dialog, cols, rows);
  lv_obj_set_grid_cell(file_panel.get_container(), LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 0, 1);
  ButtonContainer *actions[] = {&close_btn, &print_btn, &delete_btn};
  for (int c = 0; c < 3; c++) {
    actions[c]->use_plain();
    actions[c]->hide_label();
    lv_obj_set_grid_cell(actions[c]->get_container(), LV_GRID_ALIGN_STRETCH, c, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  }
  lv_obj_add_style(delete_btn.get_button(), &styles().icon_danger, 0);

  lv_obj_set_size(spinner, scale_r(60), scale_r(60));
  lv_obj_set_style_arc_width(spinner, scale_r(6), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, scale_r(6), LV_PART_INDICATOR);
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);
  lv_obj_move_foreground(spinner);

  ws.register_notify_update(this);
  ws.register_method_callback("notify_filelist_changed", "PrintPanel",
      [this](json &d) { handle_file_list_change(d); });
}

PrintPanel::~PrintPanel() {
  ws.unregister_notify_update(this);
  clear_files();
  if (file_view != NULL) {
    lv_obj_del(file_view);
    file_view = NULL;
  }
  if (files_cont != NULL) {
    lv_obj_del(files_cont);
    files_cont = NULL;
  }
}

void PrintPanel::populate_files(json &j, bool available) {
  const std::string selected = cur_file == NULL ? "" : cur_file->full_path;
  cur_file = NULL;
  int scroll_y = lv_obj_get_scroll_y(file_grid);
  if (available != usb_available) {
    source_scroll_y[active_usb] = scroll_y;
    update_source_tabs(available);
    scroll_y = source_scroll_y[active_usb];
  }
  std::map<std::string, PrintFile> updated;
  for (const auto &f : j["result"]) {
    const std::string path = f["path"];
    const double modified = f["modified"].get<double>();
    const uint64_t size = f.value("size", uint64_t(0));
    auto previous = files.find(path);
    if (previous != files.end() && previous->second.date_modified == modified && previous->second.size == size) {
      updated.emplace(path, std::move(previous->second));
    } else {
      if (previous != files.end()) {
        invalidate_thumbnail(previous->second);
      }
      updated.emplace(path, PrintFile{path.substr(path.find_last_of('/') + 1), path, modified, size});
    }
  }
  for (const auto &previous : files) {
    if (updated.find(previous.first) == updated.end()) {
      invalidate_thumbnail(previous.second);
    }
  }
  files = std::move(updated);
  show_files(scroll_y);
  if (!selected.empty()) {
    if (auto *file = find_file(selected); file != NULL && usb_file(selected) == active_usb) {
      show_file_detail(file);
    } else {
      show_browser();
    }
  }
}

void PrintPanel::handle_file_list_change(json &j) {
  if (j["/params/0/item/root"_json_pointer] != "gcodes") {
    return;
  }
  // Notifications arrive on the websocket thread, like the list response.
  std::lock_guard<std::mutex> lock(lv_lock);
  subscribe();
}

void PrintPanel::consume(json &j) {
  if (j["/params/0/print_stats/state"_json_pointer].is_null()) {
    return;
  }
  std::lock_guard<std::mutex> lock(lv_lock);
  if (visible && printer_busy()) {
    background();
  }
  update_actions();
}

bool PrintPanel::printer_busy() {
  const auto &state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  return state == "printing" || state == "paused";
}

void PrintPanel::update_actions() {
  const auto &state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  const bool busy = state == "printing" || state == "paused";
  if (!state.is_null() && !busy && cur_file != NULL && cur_file->contains_metadata() && !refreshing_files) {
    print_btn.enable();
  } else {
    print_btn.disable();
  }
  if (cur_file != NULL && !refreshing_files) {
    delete_btn.enable();
  } else {
    delete_btn.disable();
  }
}

void PrintPanel::subscribe() {
  refresh_pending = true;
  if (!visible || refreshing_files) {
    return;
  }
  refreshing_files = true;
  refresh_pending = false;
  update_actions();
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  const uint64_t generation = refresh_generation;
  ws.send_jsonrpc("server.files.list", {{"root", "gcodes"}}, [this, generation](json &d) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (!visible || generation != refresh_generation) {
      return;
    }
    if (d.contains("result") && d["result"].is_array()) {
      // A file list cannot distinguish a missing USB directory from an empty
      // one. Ask the printer, rather than inspecting the UI host's filesystem.
      ws.send_jsonrpc("server.files.get_directory", {{"path", "gcodes"}},
          [this, generation, listing = d](json &directory) mutable {
        std::lock_guard<std::mutex> lock(lv_lock);
        if (!visible || generation != refresh_generation) {
          return;
        }
        bool available = usb_available;
        const auto dirs = directory.value("/result/dirs"_json_pointer, json());
        if (dirs.is_array()) {
          available = std::any_of(dirs.begin(), dirs.end(), [](const json &dir) {
            return dir.value("dirname", std::string()) == "usb";
          });
        } else {
          // Keep files reachable if directory information is unavailable.
          for (const auto &file : listing["result"]) {
            available = available || usb_file(file["path"].get<std::string>());
          }
        }
        populate_files(listing, available);
        finish_refresh();
      });
      return;
    } else if (visible) {
      create_simple_dialog(lv_layer_top(), "File list failed",
          d.value("/error/message"_json_pointer, std::string("Moonraker could not list the files")).c_str(), true, true);
    }
    finish_refresh();
  });
}

void PrintPanel::finish_refresh() {
  refreshing_files = false;
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  update_actions();
  if (refresh_pending) {
    subscribe();
  } else {
    load_visible_metadata();
  }
}

void PrintPanel::update_source_tabs(bool available) {
  usb_available = available;
  if (!usb_available) {
    active_usb = false;
    source_scroll_y[1] = 0;
  }
  // LVGL 8 can add tabs but cannot remove them. Keep the gallery alive while
  // replacing the tabview when the USB directory appears or disappears.
  lv_obj_set_parent(browser_view, files_cont);
  if (source_tabs != NULL) {
    lv_obj_del(source_tabs);
  }
  source_tabs = lv_tabview_create(files_cont, LV_DIR_TOP, scale_r(36));
  lv_obj_set_size(source_tabs, LV_PCT(100), LV_PCT(100));
  style_embedded_tabview(source_tabs);
  if (!usb_available) {
    lv_obj_add_flag(lv_tabview_get_tab_btns(source_tabs), LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(lv_tabview_get_content(source_tabs), LV_OBJ_FLAG_SCROLLABLE);
  printer_tab = lv_tabview_add_tab(source_tabs, "Printer");
  usb_tab = usb_available ? lv_tabview_add_tab(source_tabs, "USB") : NULL;
  for (auto *page : {printer_tab, usb_tab}) {
    if (page != NULL) {
      lv_obj_set_style_pad_all(page, gap(), 0);
      lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
  lv_obj_set_parent(browser_view, active_usb ? usb_tab : printer_tab);
  lv_tabview_set_act(source_tabs, active_usb ? 1 : 0, LV_ANIM_OFF);
  lv_obj_add_event_cb(source_tabs, [](lv_event_t *e) {
    auto *panel = static_cast<PrintPanel *>(lv_event_get_user_data(e));
    auto *target = lv_event_get_target(e);
    if (target == panel->source_tabs || target == lv_tabview_get_tab_btns(panel->source_tabs)) {
      panel->select_source();
    }
  }, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_move_foreground(spinner);
}

void PrintPanel::select_source() {
  const bool next_usb = usb_available && lv_tabview_get_tab_act(source_tabs) == 1;
  if (next_usb == active_usb) {
    return;
  }
  source_scroll_y[active_usb] = lv_obj_get_scroll_y(file_grid);
  active_usb = next_usb;
  metadata_pending = true; // suppress preview requests during reparenting
  lv_obj_set_parent(browser_view, active_usb ? usb_tab : printer_tab);
  show_files(source_scroll_y[active_usb]);
  show_browser();
}

void PrintPanel::foreground() {
  if (printer_busy() || visible) {
    return;
  }
  visible = true;
  update_actions();
  lv_obj_clear_flag(files_cont, LV_OBJ_FLAG_HIDDEN);
  subscribe();
}

void PrintPanel::background() {
  visible = false;
  show_browser();
  clear_files();
  lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(files_cont, LV_OBJ_FLAG_HIDDEN);
}

void PrintPanel::invalidate_thumbnail(const PrintFile &file) {
  if (!file.thumbnail_path.empty()) {
    lv_img_cache_invalidate_src(("A:" + file.thumbnail_path).c_str());
  }
}

void PrintPanel::clear_files() {
  updating_tiles = true;
  ++view_generation;
  ++refresh_generation;
  metadata_pending = false;
  refreshing_files = false;
  refresh_pending = false;
  cur_file = NULL;
  file_panel.clear_view("", "");
  for (const auto &file : files) {
    invalidate_thumbnail(file.second);
  }
  tiles.clear();
  file_order.clear();
  scroll_extent = NULL;
  tile_offset = 0;
  if (file_grid != NULL) {
    lv_obj_clean(file_grid);
  }
  files.clear();
  tiles.shrink_to_fit();
  file_order.shrink_to_fit();
  source_scroll_y[0] = source_scroll_y[1] = 0;
  if (files_cont != NULL && active_usb) {
    active_usb = false;
    lv_obj_set_parent(browser_view, printer_tab);
    lv_tabview_set_act(source_tabs, 0, LV_ANIM_OFF);
  }
  updating_tiles = false;
}

PrintPanel::PrintFile *PrintPanel::find_file(const std::string &path) {
  auto file = files.find(path);
  return file == files.end() ? NULL : &file->second;
}

void PrintPanel::show_files(int scroll_y) {
  updating_tiles = true;
  ++view_generation;
  // An unanswered request from an earlier view (or connection) must not
  // prevent previews in this one from loading.
  metadata_pending = false;
  cur_file = NULL;
  std::map<std::string, FileTile> previous_tiles;
  for (auto &tile : tiles) {
    const std::string path = tile.path;
    previous_tiles.emplace(path, std::move(tile));
  }
  tiles.clear();
  if (scroll_extent != NULL) {
    lv_obj_del(scroll_extent);
    scroll_extent = NULL;
  }
  if (previous_tiles.empty()) {
    lv_obj_clean(file_grid); // remove the empty-state label
  }
  std::vector<PrintFile *> entries;
  for (auto &file : files) {
    if (usb_file(file.first) == active_usb) {
      entries.push_back(&file.second);
    }
  }
  std::sort(entries.begin(), entries.end(), [](const PrintFile *a, const PrintFile *b) {
    return a->date_modified != b->date_modified ? a->date_modified > b->date_modified : a->full_path < b->full_path;
  });
  file_order.clear();
  file_order.reserve(entries.size());
  for (auto *entry : entries) {
    file_order.push_back(entry->full_path);
  }
  lv_obj_update_layout(browser_view);
  tile_columns = std::clamp(lv_obj_get_content_width(file_grid) / std::max(scale_r(95), 128), 3, 4);
  tile_width = (lv_obj_get_content_width(file_grid) - (tile_columns - 1) * gap()) / tile_columns;
  tile_height = std::max(scale_r(96), (lv_obj_get_content_height(file_grid) - gap()) / 2);
  // Small galleries retain their existing widgets. Large libraries use only
  // four rows, recycling those widgets as files enter and leave the viewport.
  const bool virtual_gallery = entries.size() > 24;
  lv_obj_set_layout(file_grid, virtual_gallery ? 0 : LV_LAYOUT_FLEX);
  const size_t tile_count = virtual_gallery ? tile_columns * 4 : entries.size();
  if (virtual_gallery) {
    const size_t rows = (entries.size() + tile_columns - 1) / tile_columns;
    const int total_height = rows * (tile_height + gap()) - gap();
    scroll_y = std::clamp(scroll_y, 0, std::max(0, total_height - lv_obj_get_content_height(file_grid)));
  }
  tile_offset = tile_offset_for_scroll(scroll_y, tile_count);
  tiles.reserve(tile_count);
  for (size_t i = 0; i < tile_count; ++i) {
    auto *entry = entries[tile_offset + i];
    auto previous = previous_tiles.find(entry->full_path);
    if (previous != previous_tiles.end()) {
      auto tile = std::move(previous->second);
      previous_tiles.erase(previous);
      lv_obj_set_size(tile.container, tile_width, tile_height);
      tiles.push_back(std::move(tile));
    } else {
      tiles.push_back(create_tile(tile_width, tile_height));
    }
    assign_tile(tiles.back(), *entry);
  }
  for (const auto &tile : previous_tiles) {
    lv_obj_del(tile.second.container);
  }
  for (size_t i = 0; i < tiles.size(); ++i) {
    lv_obj_move_to_index(tiles[i].container, i);
  }
  const auto alignment = entries.empty() ? LV_FLEX_ALIGN_CENTER : LV_FLEX_ALIGN_START;
  if (!virtual_gallery) {
    lv_obj_set_flex_align(file_grid, alignment, alignment, alignment);
  }
  if (virtual_gallery) {
    // A transparent marker preserves the full scroll range without creating
    // widgets for every file. It does not receive touches.
    scroll_extent = lv_obj_create(file_grid);
    lv_obj_remove_style_all(scroll_extent);
    lv_obj_clear_flag(scroll_extent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(scroll_extent, 1, 1);
    const size_t rows = (entries.size() + tile_columns - 1) / tile_columns;
    lv_obj_set_pos(scroll_extent, 0, rows * (tile_height + gap()) - gap() - 1);
  } else if (entries.empty()) {
    auto *label = lv_label_create(file_grid);
    lv_label_set_text(label, "No G-code files");
  }
  lv_obj_update_layout(file_grid);
  lv_obj_scroll_to_y(file_grid, scroll_y, LV_ANIM_OFF);
  updating_tiles = false;
  update_visible_tiles();
  refresh_scroll(file_grid);
  update_actions();
  load_visible_metadata();
}

PrintPanel::FileTile PrintPanel::create_tile(int width, int height) {
  auto *tile = lv_obj_create(file_grid);
  lv_obj_add_style(tile, &styles().card, 0);
  lv_obj_add_style(tile, &styles().card_pressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(tile, col(RAISED), 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_set_style_pad_row(tile, 0, 0);
  lv_obj_set_style_clip_corner(tile, true, 0);
  lv_obj_set_size(tile, width, height);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(tile, _handle_callback, LV_EVENT_LONG_PRESSED, this);
  auto *preview_box = create_row(tile);
  lv_obj_set_size(preview_box, LV_PCT(100), 0);
  lv_obj_set_flex_grow(preview_box, 1);
  lv_obj_set_style_bg_color(preview_box, col(SURFACE), 0);
  lv_obj_set_style_bg_opa(preview_box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(preview_box, 0, 0);
  lv_obj_clear_flag(preview_box, LV_OBJ_FLAG_CLICKABLE);
  auto *preview = lv_img_create(preview_box);
  lv_img_set_src(preview, LV_SYMBOL_FILE);
  lv_obj_set_style_text_font(preview, scale_font(28), 0);
  lv_obj_set_style_text_color(preview, col(TEXT_DIM), 0);
  lv_obj_center(preview);
  auto *name = lv_label_create(tile);
  lv_obj_set_width(name, LV_PCT(100));
  lv_obj_set_style_text_font(name, scale_font(12), 0);
  lv_obj_set_height(name, lv_font_get_line_height(scale_font(12)) + scale_r(4));
  lv_obj_set_style_pad_hor(name, scale_r(4), 0);
  lv_obj_set_style_pad_ver(name, scale_r(2), 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_anim_speed(name, scale_r(30), 0);
  auto *footer = create_row(tile);
  const auto *footer_font = scale_font(10);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(footer, LV_PCT(100), lv_font_get_line_height(footer_font) + gap());
  lv_obj_set_style_pad_hor(footer, scale_r(4), 0);
  lv_obj_set_style_pad_bottom(footer, gap(), 0);
  lv_obj_set_style_pad_column(footer, scale_r(4), 0);
  lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
  auto *material = lv_label_create(footer);
  auto *time = lv_label_create(footer);
  auto *weight = lv_label_create(footer);
  for (auto *field : {material, time, weight}) {
    lv_obj_add_style(field, &styles().dim_label, 0);
    lv_obj_set_style_text_font(field, footer_font, 0);
    lv_obj_set_size(field, LV_SIZE_CONTENT, lv_font_get_line_height(footer_font));
    lv_label_set_long_mode(field, LV_LABEL_LONG_DOT);
  }
  lv_obj_set_width(material, 0);
  lv_obj_set_flex_grow(material, 1);
  lv_label_set_long_mode(material, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_anim_speed(material, scale_r(30), 0);
  lv_obj_set_style_text_align(weight, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(time, "");
  lv_label_set_text(weight, "");
  FileTile result{"", tile, preview, material, time, weight};
  result.name = name;
  return result;
}

void PrintPanel::assign_tile(FileTile &tile, const PrintFile &file) {
  stop_tile_spinner(tile);
  tile.path = file.full_path;
  const auto caption = tile_caption(file.name);
  tile.filename_material = caption.second;
  lv_label_set_text(tile.name, caption.first.c_str());
  tile.requested = file.contains_metadata();
  update_tile(tile, file);
}

size_t PrintPanel::tile_offset_for_scroll(int scroll_y, size_t tile_count) const {
  const size_t row = std::max(0, scroll_y) / (tile_height + gap());
  // Keep one row above the viewport and a row ahead of the visible page.
  return std::min((row > 0 ? row - 1 : 0) * tile_columns, file_order.size() - tile_count);
}

void PrintPanel::update_visible_tiles() {
  if (!visible || updating_tiles || scroll_extent == NULL) {
    return;
  }
  updating_tiles = true;
  const size_t offset = tile_offset_for_scroll(lv_obj_get_scroll_y(file_grid), tiles.size());
  const int64_t shift = static_cast<int64_t>(offset) - static_cast<int64_t>(tile_offset);
  if (shift > 0 && shift < static_cast<int64_t>(tiles.size())) {
    std::rotate(tiles.begin(), tiles.begin() + shift, tiles.end());
  } else if (shift < 0 && -shift < static_cast<int64_t>(tiles.size())) {
    std::rotate(tiles.begin(), tiles.end() + shift, tiles.end());
  }
  tile_offset = offset;
  for (size_t i = 0; i < tiles.size(); ++i) {
    const size_t index = tile_offset + i;
    if (tiles[i].path != file_order[index]) {
      assign_tile(tiles[i], *find_file(file_order[index]));
    }
    lv_obj_set_pos(tiles[i].container, (index % tile_columns) * (tile_width + gap()),
        (index / tile_columns) * (tile_height + gap()));
  }
  lv_obj_update_layout(file_grid);
  updating_tiles = false;
}

void PrintPanel::update_tile(FileTile &tile, const PrintFile &file) {
  tile.loading = !file.contains_metadata();
  if (!tile.loading) {
    stop_tile_spinner(tile);
  }
  if (!file.contains_metadata()) {
    lv_label_set_text(tile.material, tile.filename_material.empty() ? "Loading..." : tile.filename_material.c_str());
    lv_label_set_text(tile.time, "");
    lv_label_set_text(tile.weight, "");
    lv_img_set_src(tile.preview, LV_SYMBOL_FILE);
    fit_img(tile.preview, scale_r(32), scale_r(32));
    lv_obj_center(tile.preview);
    return;
  }
  const auto &metadata = file.metadata["result"];
  const auto eta_field = metadata.find("estimated_time");
  const auto weight_field = metadata.find("filament_weight_total");
  const int64_t eta = eta_field != metadata.end() && eta_field->is_number() ? eta_field->get<int64_t>() : 0;
  const double weight = weight_field != metadata.end() && weight_field->is_number() ? weight_field->get<double>() : 0.0;
  const std::string material = tile.filename_material.empty() ? metadata_material(metadata) : tile.filename_material;
  lv_label_set_text(tile.material, material.empty() ? "Unknown" : material.c_str());
  lv_label_set_text(tile.time, tile_time(eta).c_str());
  lv_label_set_text(tile.weight, weight > 0 ? fmt::format("{:.1f} g", weight).c_str() : "? g");
  if (!file.thumbnail_path.empty()) {
    const std::string source = "A:" + file.thumbnail_path;
    const char *current_source = static_cast<const char *>(lv_img_get_src(tile.preview));
    if (current_source == NULL || std::strcmp(current_source, source.c_str()) != 0) {
      lv_img_set_src(tile.preview, source.c_str());
    }
    auto *box = lv_obj_get_parent(tile.preview);
    lv_obj_update_layout(tile.container);
    fit_img(tile.preview, lv_obj_get_content_width(box), lv_obj_get_content_height(box), 2 * LV_IMG_ZOOM_NONE);
    lv_obj_center(tile.preview);
  } else {
    lv_img_set_src(tile.preview, LV_SYMBOL_FILE);
    fit_img(tile.preview, scale_r(32), scale_r(32));
    lv_obj_center(tile.preview);
  }
}

void PrintPanel::stop_tile_spinner(FileTile &tile) {
  if (tile.loading_spinner != NULL) {
    auto *loading = tile.loading_spinner;
    tile.loading_spinner = NULL;
    lv_obj_del(loading);
  }
  lv_obj_clear_flag(tile.preview, LV_OBJ_FLAG_HIDDEN);
}

void PrintPanel::update_tile_animations() {
  if (!visible) {
    return;
  }
  for (auto &tile : tiles) {
    const bool shown = lv_obj_is_visible(tile.container);
    const auto name_mode = shown ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP;
    for (auto *label : {tile.name, tile.material}) {
      if (lv_label_get_long_mode(label) != name_mode) {
        lv_label_set_long_mode(label, name_mode);
      }
    }
    if (!tile.loading || !shown) {
      stop_tile_spinner(tile);
    } else if (tile.loading_spinner == NULL) {
      tile.loading_spinner = lv_spinner_create(lv_obj_get_parent(tile.preview), 1000, 60);
      lv_obj_set_size(tile.loading_spinner, scale_r(24), scale_r(24));
      lv_obj_set_style_arc_width(tile.loading_spinner, scale_r(3), LV_PART_MAIN);
      lv_obj_set_style_arc_width(tile.loading_spinner, scale_r(3), LV_PART_INDICATOR);
      lv_obj_center(tile.loading_spinner);
      lv_obj_add_flag(tile.preview, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void PrintPanel::show_browser() {
  cur_file = NULL;
  if (delete_dialog != NULL) {
    simple_dialog_close(delete_dialog);
    delete_dialog = NULL;
  }
  pending_delete_path.clear();
  lv_obj_add_flag(file_view, LV_OBJ_FLAG_HIDDEN);
  file_panel.clear_view("", "");
  lv_obj_update_layout(browser_view);
  refresh_scroll(file_grid);
  load_visible_metadata();
}

void PrintPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED || !visible || refreshing_files || printer_busy()) {
    return;
  }
  auto *target = lv_event_get_current_target(e);
  auto found = std::find_if(tiles.begin(), tiles.end(), [target](const FileTile &tile) {
    return tile.container == target;
  });
  if (found == tiles.end()) {
    return;
  }
  auto *file = find_file(found->path);
  if (file == NULL) {
    return;
  }
  show_file_detail(file);
}

void PrintPanel::show_file_detail(PrintFile *file) {
  cur_file = file;
  if (lv_obj_has_flag(file_view, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_move_foreground(file_view);
  }
  lv_obj_clear_flag(file_view, LV_OBJ_FLAG_HIDDEN);
  file_panel.clear_view(file->full_path);
  detail_requested = false;
  if (file->contains_metadata()) {
    file_panel.refresh_view(file->metadata, file->full_path, file->thumbnail_path);
  }
  for (auto &tile : tiles) {
    if (tile.path == file->full_path) {
      if (!file->contains_metadata()) {
        tile.requested = false;  // selecting a failed preview retries it
        tile.loading = true;
      }
      break;
    }
  }
  update_actions();
  load_visible_metadata();
}

void PrintPanel::load_visible_metadata() {
  if (!visible || updating_tiles) {
    return;
  }
  update_tile_animations();
  if (refreshing_files || metadata_pending) {
    return;
  }
  PrintFile *next_file = NULL;
  if (cur_file != NULL) {
    if (cur_file->contains_metadata() || detail_requested) {
      return;
    }
    // A refresh can move the selected file outside the tile window. Its
    // dialog still needs a preview, independently of the gallery widgets.
    detail_requested = true;
    next_file = cur_file;
  } else {
    for (auto &tile : tiles) {
      if (tile.requested || !lv_obj_is_visible(tile.container)) {
        continue;
      }
      tile.requested = true;
      next_file = find_file(tile.path);
      break;
    }
  }
  if (next_file == NULL) {
    return;
  }
  metadata_pending = true;
  const std::string path = next_file->full_path;
  const uint64_t generation = view_generation;
  // Prefer the larger embedded preview over a slicer's tiny 32px icon.
  const int width = std::max(scale_r(96), 180);
  ws.send_jsonrpc("server.files.metadata", {{"filename", path}}, [this, path, generation, width](json &data) {
    const auto message = data.value("/error/message"_json_pointer, std::string());
    if (data.value("/error/code"_json_pointer, 0) == 404 || message.find("Metadata not available for") == 0) {
      std::lock_guard<std::mutex> lock(lv_lock);
      if (!visible || generation != view_generation) {
        return;
      }
      // Files on a newly mounted USB key can be listed before Moonraker has
      // indexed them. Scan only this visible file, keeping the preview pending.
      LOG_DEBUG("Scanning missing metadata for {}", path);
      ws.send_jsonrpc("server.files.metascan", {{"filename", path}}, [this, path, generation, width](json &scanned) {
        handle_metadata(path, generation, width, scanned);
      });
      return;
    }
    handle_metadata(path, generation, width, data);
  });
}

void PrintPanel::handle_metadata(const std::string &path, uint64_t generation, int width, json &data) {
  // Thumbnail downloads can block on a remote printer. Keep them outside the
  // LVGL lock, and never keep file/widget pointers across that unlocked work.
  bool current;
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    current = visible && generation == view_generation;
  }
  const bool success = data.contains("result") && data["result"].is_object();
  std::string thumbnail;
  if (current && success) {
    thumbnail = KUtils::get_thumbnail(path, data, width).first;
  }
  std::lock_guard<std::mutex> lock(lv_lock);
  if (generation == view_generation) {
    metadata_pending = false;
  }
  if (current && visible && generation == view_generation) {
    auto *file = find_file(path);
    if (file != NULL && success) {
      // A request can finish after its tile has been recycled by scrolling.
      // Keep the result cached for when this file comes back into view.
      file->metadata = data;
      file->thumbnail_path = thumbnail;
      invalidate_thumbnail(*file);
    }
    for (auto &tile : tiles) {
      if (tile.path != path || file == NULL) {
        continue;
      }
      tile.requested = true;
      if (success) {
        update_tile(tile, *file);
      } else {
        tile.loading = false;
        stop_tile_spinner(tile);
        lv_label_set_text(tile.material, tile.filename_material.empty() ? "Unavailable" : tile.filename_material.c_str());
        lv_label_set_text(tile.time, "");
        lv_label_set_text(tile.weight, "");
      }
      break;
    }
    if (file != NULL && cur_file == file) {
      detail_requested = true;
      if (success) {
        file_panel.refresh_view(file->metadata, path, thumbnail);
      } else {
        file_panel.clear_view(path, "Could not load file details. Reopen to retry.");
      }
    }
    update_actions();
  }
  load_visible_metadata();
}

void PrintPanel::confirm_delete() {
  if (cur_file == NULL || refreshing_files) {
    return;
  }
  pending_delete_path = cur_file->full_path;
  static const char *buttons[] = {"OK", "Cancel", ""};
  SimpleDialogOptions options;
  options.buttons = buttons;
  options.highlighted_button_idx = 0;
  options.result_cb = _delete_file;
  options.user_data = this;
  delete_dialog = create_configurable_dialog(lv_layer_top(), "Delete file",
      fmt::format("Delete {}?", cur_file->name).c_str(), options);
}

void PrintPanel::delete_file(uint32_t index) {
  const std::string path = pending_delete_path;
  pending_delete_path.clear();
  if (index != 0 || path.empty()) {
    return;
  }
  ws.send_jsonrpc("server.files.delete_file", {{"path", "gcodes/" + path}}, [this](json &data) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (data.contains("error")) {
      create_simple_dialog(lv_layer_top(), "Delete file failed",
          data.value("/error/message"_json_pointer, std::string("Moonraker could not delete the file")).c_str(), true, true);
      return;
    }
    show_browser();
    subscribe();
  });
}

void PrintPanel::handle_back_btn(lv_event_t *) {
  show_browser();
}

void PrintPanel::handle_print_callback(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED || cur_file == NULL ||
      !cur_file->contains_metadata() || refreshing_files) {
    return;
  }
  const auto &state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  if (state.is_null() || state == "printing" || state == "paused") {
    return;
  }
  const std::string path = cur_file->full_path;
  const std::string name = cur_file->name;
  const json metadata = cur_file->metadata;
  show_browser();
  if (mmu == NULL || !mmu->open_tool_map(metadata, name, [this, path]() { start_print(path); })) {
    start_print(path);
  }
}

void PrintPanel::start_print(const std::string &path) {
  const auto &state = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  if (!state.is_null() && state != "printing" && state != "paused") {
    background();
    ws.send_jsonrpc("printer.print.start", {{"filename", path}});
    if (print_started) {
      print_started();
    }
    print_status.foreground();
  }
}
