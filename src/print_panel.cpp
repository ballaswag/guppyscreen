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
  }, LV_EVENT_DELETE, this);

  lv_obj_set_size(browser_view, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(browser_view, LV_FLEX_FLOW_COLUMN);

  file_grid = create_row(browser_view);
  lv_obj_set_size(file_grid, LV_PCT(100), 0);
  lv_obj_set_flex_grow(file_grid, 1);
  lv_obj_set_flex_flow(file_grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(file_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  // Keep empty space on the right for swiping without touching a file.
  lv_obj_set_style_pad_right(file_grid, scale_r(18), 0);
  manage_scroll(file_grid);
  lv_obj_add_event_cb(file_grid, [](lv_event_t *e) {
    static_cast<PrintPanel *>(lv_event_get_user_data(e))->update_tile_animations();
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

void PrintPanel::populate_files(json &j) {
  const std::string selected = cur_file == NULL ? "" : cur_file->full_path;
  cur_file = NULL;
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
  show_files();
  if (!selected.empty()) {
    if (auto *file = find_file(selected)) {
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
  const uint64_t generation = view_generation;
  ws.send_jsonrpc("server.files.list", {{"root", "gcodes"}}, [this, generation](json &d) {
    std::lock_guard<std::mutex> lock(lv_lock);
    if (!visible || generation != view_generation) {
      return;
    }
    if (d.contains("result") && d["result"].is_array()) {
      populate_files(d);
    } else if (visible) {
      create_simple_dialog(lv_layer_top(), "File list failed",
          d.value("/error/message"_json_pointer, std::string("Moonraker could not list the files")).c_str(), true, true);
    }
    refreshing_files = false;
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    update_actions();
    if (refresh_pending) {
      subscribe();
    } else {
      load_visible_metadata();
    }
  });
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
  ++view_generation;
  metadata_pending = false;
  refreshing_files = false;
  refresh_pending = false;
  cur_file = NULL;
  file_panel.clear_view("", "");
  for (const auto &file : files) {
    invalidate_thumbnail(file.second);
  }
  tiles.clear();
  if (file_grid != NULL) {
    lv_obj_clean(file_grid);
  }
  files.clear();
  tiles.shrink_to_fit();
}

PrintPanel::PrintFile *PrintPanel::find_file(const std::string &path) {
  auto file = files.find(path);
  return file == files.end() ? NULL : &file->second;
}

void PrintPanel::show_files() {
  ++view_generation;
  // An unanswered request from an earlier view (or connection) must not
  // prevent previews in this one from loading.
  metadata_pending = false;
  cur_file = NULL;
  const int scroll_y = lv_obj_get_scroll_y(file_grid);
  std::map<std::string, FileTile> previous_tiles;
  for (auto &tile : tiles) {
    const std::string path = tile.path;
    previous_tiles.emplace(path, std::move(tile));
  }
  tiles.clear();
  if (previous_tiles.empty()) {
    lv_obj_clean(file_grid); // remove the empty-state label
  }
  std::vector<PrintFile *> entries;
  for (auto &file : files) {
    entries.push_back(&file.second);
  }
  std::sort(entries.begin(), entries.end(), [](const PrintFile *a, const PrintFile *b) {
    return a->date_modified != b->date_modified ? a->date_modified > b->date_modified : a->full_path < b->full_path;
  });
  lv_obj_update_layout(browser_view);
  const int columns = std::clamp(lv_obj_get_content_width(file_grid) / std::max(scale_r(95), 128), 3, 4);
  const int tile_w = (lv_obj_get_content_width(file_grid) - (columns - 1) * gap()) / columns;
  const int tile_h = std::max(scale_r(96), (lv_obj_get_content_height(file_grid) - gap()) / 2);
  tiles.reserve(entries.size());
  for (auto *entry : entries) {
    auto previous = previous_tiles.find(entry->full_path);
    if (previous != previous_tiles.end()) {
      auto tile = std::move(previous->second);
      previous_tiles.erase(previous);
      lv_obj_set_size(tile.container, tile_w, tile_h);
      tiles.push_back(std::move(tile));
      continue;
    }
    auto *tile = lv_obj_create(file_grid);
    lv_obj_add_style(tile, &styles().card, 0);
    lv_obj_add_style(tile, &styles().card_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(tile, col(RAISED), 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_pad_row(tile, 0, 0);
    lv_obj_set_style_clip_corner(tile, true, 0);
    lv_obj_set_size(tile, tile_w, tile_h);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(tile, _handle_callback, LV_EVENT_SHORT_CLICKED, this);
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
    const auto caption = tile_caption(entry->name);
    lv_label_set_text(name, caption.first.c_str());
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
    lv_label_set_text(material, caption.second.empty() ? "Loading..." : caption.second.c_str());
    lv_label_set_text(time, "");
    lv_label_set_text(weight, "");
    tiles.push_back({entry->full_path, tile, preview, material, time, weight});
    tiles.back().filename_material = caption.second;
    tiles.back().name = name;
  }
  for (const auto &tile : previous_tiles) {
    lv_obj_del(tile.second.container);
  }
  for (size_t i = 0; i < tiles.size(); ++i) {
    lv_obj_move_to_index(tiles[i].container, i);
    auto *file = find_file(tiles[i].path);
    tiles[i].requested = file->contains_metadata();
    update_tile(tiles[i], *file);
  }
  if (entries.empty()) {
    auto *label = lv_label_create(file_grid);
    lv_label_set_text(label, "No G-code files");
  }
  lv_obj_update_layout(file_grid);
  lv_obj_scroll_to_y(file_grid, scroll_y, LV_ANIM_OFF);
  refresh_scroll(file_grid);
  update_actions();
  load_visible_metadata();
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
  if (!visible || refreshing_files || printer_busy()) {
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
  for (auto &tile : tiles) {
    if (tile.path == file->full_path) {
      if (file->contains_metadata()) {
        file_panel.refresh_view(file->metadata, file->full_path, file->thumbnail_path);
      } else {
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
  if (!visible) {
    return;
  }
  update_tile_animations();
  if (refreshing_files || metadata_pending) {
    return;
  }
  for (auto &tile : tiles) {
    if (tile.requested) {
      continue;
    }
    if (cur_file != NULL ? tile.path != cur_file->full_path : !lv_obj_is_visible(tile.container)) {
      continue;
    }
    tile.requested = true;
    metadata_pending = true;
    const std::string path = tile.path;
    const uint64_t generation = view_generation;
    // Prefer the larger embedded preview over a slicer's tiny 32px icon.
    const int width = std::max(scale_r(96), 180);
    ws.send_jsonrpc("server.files.metadata", {{"filename", path}}, [this, path, generation, width](json &data) {
      handle_metadata(path, generation, width, data);
    });
    return;
  }
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
    for (auto &tile : tiles) {
      if (tile.path != path || file == NULL) {
        continue;
      }
      tile.requested = true;
      if (success) {
        file->metadata = data;
        file->thumbnail_path = thumbnail;
        invalidate_thumbnail(*file);
        update_tile(tile, *file);
        if (cur_file == file) {
          file_panel.refresh_view(file->metadata, path, thumbnail);
        }
      } else {
        tile.loading = false;
        stop_tile_spinner(tile);
        lv_label_set_text(tile.material, tile.filename_material.empty() ? "Unavailable" : tile.filename_material.c_str());
        lv_label_set_text(tile.time, "");
        lv_label_set_text(tile.weight, "");
        if (cur_file == file) {
          file_panel.clear_view(path, "Could not load file details. Reopen to retry.");
        }
      }
      break;
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
