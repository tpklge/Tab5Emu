#include "gui.hpp"
#include <algorithm>
tab5::library::Page Gui::query_catalog(const tab5::library::Query &query) {
  tab5::library::Page result;
  if (query.text.size() > (query.exact ? 320u : 128u) || query.system.size() > 16 || query.page > 100000 / 64)
    return result;
  {
    std::lock_guard<std::recursive_mutex> ui(mutex_);
    if (paused_ || Tab5Emu::get().usb_msc_host_has_card()) {
      result.error = "Biblioteca indisponivel durante jogo ou acesso USB"; return result;
    }
    if (!indexed_library_) {
      const auto search = tab5::library::fold(query.text);
      const size_t first = query.page * tab5::library::page_size;
      for (const auto &rom : rom_infos_) {
        const auto key = tab5::input::profile_system(console_info(console_of(rom.platform)).key);
        if (!query.system.empty() && query.system != key) continue;
        if (!search.empty() && (query.exact
                ? (tab5::library::fold(rom.name) != search && tab5::library::fold(rom.rom_path) != search && tab5::library::fold(std::string(tab5::content::root) + "/" + query.text) != tab5::library::fold(rom.rom_path))
                : tab5::library::fold(rom.name + " " + rom.description + " " + rom.rom_path).find(search) == std::string::npos)) continue;
        if (result.total++ >= first && result.games.size() < tab5::library::page_size)
          result.games.push_back(rom);
      }
      result.valid = true; result.catalog_total = rom_infos_.size(); result.number = query.page;
      return result;
    }
  }
  auto job = std::make_shared<CatalogJob>(); job->query = query;
  std::unique_lock<std::mutex> work(cover_mutex_);
  if (cover_paused_ || cover_stop_ || catalog_job_) return result;
  catalog_job_ = job; cover_cv_.notify_all();
  if (!cover_cv_.wait_for(work, std::chrono::seconds(15), [&] { return job->done || cover_paused_ || cover_stop_; })) {
    job->cancelled = true;
    result.error = "Consulta cancelada por tempo limite"; return result;
  }
  if (!job->done) { job->cancelled = true; return result; }
  return std::move(job->result);
}
bool Gui::select_catalog_rom(const RomInfo &rom) {
  std::lock_guard<std::recursive_mutex> ui(mutex_);
  if (paused_ || Tab5Emu::get().usb_msc_host_has_card()) return false;
  if (!indexed_library_) return select_rom_matching(rom.rom_path);
  {
    std::lock_guard<std::mutex> work(cover_mutex_);
    ++library_generation_; library_job_ = library_ready_ = false;
    library_loading_ = false;
  }
  // Retain only the selected record; opening the library again reloads its
  // ordinary page. The remote lookup never allocates the complete catalog.
  visible_indices_.clear(); focused_rom_ = -1;
  launcher_->close_picker(); launcher_->set_filter(std::nullopt);
  rom_infos_ = {rom};
  AppSettings::get().modify_ui([](UiSettings &ui) { ui.search.clear(); ui.library_view = 0; });
  show_games(rom.rom_path);
  return true;
}
void Gui::build_library_pages() {
  library_pages_ = lv_obj_create(launcher_->screen());
  lv_obj_remove_style_all(library_pages_);
  lv_obj_set_pos(library_pages_, 510, 604);
  lv_obj_set_size(library_pages_, 745, 48);
  lv_obj_set_flex_flow(library_pages_, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(library_pages_, 6, 0);
  for (int direction : {-1, 1}) {
    auto *b = lv_button_create(library_pages_);
    lv_obj_set_size(b, 70, 42);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x30394c),0);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(intptr_t(direction)));
    auto *label = lv_label_create(b);
    lv_label_set_text(label, direction < 0 ? "<" : ">");
    lv_obj_center(label);
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *e) {
          auto *g = static_cast<Gui *>(lv_event_get_user_data(e));
          const int d = int(reinterpret_cast<intptr_t>(
              lv_obj_get_user_data(lv_event_get_target_obj(e))));
          if (d < 0 && g->library_page_ == 0)
            return;
          if (d > 0 && (g->library_page_ + 1) * tab5::library::page_size >=
                           g->library_total_)
            return;
          g->request_library_page(g->library_page_ + d);
        },
        LV_EVENT_CLICKED, this);
  }
  library_page_label_ = lv_label_create(library_pages_);
  lv_obj_set_width(library_page_label_, 175);
  lv_obj_set_style_text_font(library_page_label_, metrics_.font_small, 0);
  library_system_ = lv_dropdown_create(library_pages_);
  lv_obj_set_size(library_system_, 200, 42);
  lv_dropdown_set_options(
      library_system_,
      "Todos os sistemas\nGame Boy\nGame Boy Color\nGBA\nMega Drive\nSNES\nNintendo 64");
  lv_obj_set_style_text_font(library_system_, metrics_.font_small, 0);
  lv_obj_add_event_cb(
      library_system_,
      [](lv_event_t *e) {
        auto *g = static_cast<Gui *>(lv_event_get_user_data(e));
        const char *keys[] = {"", "gb", "gbc", "gba", "megadrive", "snes", "n64"};
        {
          std::lock_guard<std::mutex> job(g->cover_mutex_);
          g->library_query_.system =
              keys[lv_dropdown_get_selected(g->library_system_)];
        }
        g->launcher_->set_filter(std::nullopt);
        g->request_library_page(0);
      },
      LV_EVENT_VALUE_CHANGED, this);
  if (indexed_library_) {
    lv_label_set_text(library_page_label_, "Carregando...");
    lv_obj_remove_flag(library_pages_, LV_OBJ_FLAG_HIDDEN);
  } else
    lv_obj_add_flag(library_pages_, LV_OBJ_FLAG_HIDDEN);
}
void Gui::request_library_page(size_t page) {
  if (!indexed_library_)
    return;
  launcher_->set_play_enabled(false);
  library_loading_ = true;
  const auto settings = AppSettings::get().ui_settings();
  {
    std::lock_guard<std::mutex> job(cover_mutex_);
    library_query_.text = settings.search;
    library_query_.view = settings.library_view;
    library_query_.favorites = settings.favorites;
    library_query_.recent = settings.recent;
    library_query_.page = page;
    library_job_ = true;
    library_ready_ = false;
    ++library_generation_;
    library_page_ = page;
  }
  if (library_pages_)
    lv_obj_remove_flag(library_pages_, LV_OBJ_FLAG_HIDDEN);
  if (library_page_label_)
    lv_label_set_text(library_page_label_, "Carregando...");
  cover_cv_.notify_all();
}
void Gui::refresh_library() {
  if (indexed_library_)
    request_library_page(0);
  else {
    auto selected = get_selected_rom();
    show_games(selected ? selected->rom_path : std::string());
  }
}
void Gui::accept_library_page() {
  tab5::library::Page result;
  {
    std::lock_guard<std::mutex> job(cover_mutex_);
    if (!library_ready_)
      return;
    library_ready_ = false;
    if (library_result_generation_ != library_generation_)
      return;
    result = std::move(library_result_);
  }
  library_loading_ = false;
  if(result.io_error){
    library_error_="Falha ao ler SD; recarregue a biblioteca";
    if(library_page_label_)lv_label_set_text(library_page_label_,"Falha de leitura");
    // An I/O failure is not a corrupt index. Preserve the current model and
    // do not enter the legacy JSON reader while the card is unavailable.
    return;
  }
  if (!result.valid) {
    library_error_ = result.error;
    indexed_library_ = false;
    rom_infos_ = load_library();
    if (library_pages_)
      lv_obj_add_flag(library_pages_, LV_OBJ_FLAG_HIDDEN);
  } else {
    if (library_system_) {
      std::string system;
      {
        std::lock_guard<std::mutex> work(cover_mutex_);
        system = library_query_.system;
      }
      const char *keys[] = {"", "gb", "gbc", "gba", "megadrive", "snes", "n64"};
      for (unsigned i = 0; i < 7; ++i)
        if (system == keys[i])
          lv_dropdown_set_selected(library_system_, i);
    }
    library_error_.clear();
    if (!result.total)
      library_page_ = 0;
    library_total_ = result.total;
    if (result.games.empty() && result.total && library_page_) {
      request_library_page((result.total - 1) / tab5::library::page_size);
      return;
    }
    rom_infos_ = std::move(result.games);
    const auto label =
        fmt::format("{}/{} | {} jogos", library_total_ ? library_page_ + 1 : 0,
                    (library_total_ + tab5::library::page_size - 1) /
                        tab5::library::page_size,
                    library_total_);
    if (library_page_label_)
      lv_label_set_text(library_page_label_, label.c_str());
    // Paging belongs above the hint bar, and single-page lists need no arrows.
    for(unsigned index=0;index<2;++index){
      auto *button=lv_obj_get_child(library_pages_,index);
      if(library_total_<=tab5::library::page_size)lv_obj_add_flag(button,LV_OBJ_FLAG_HIDDEN);
      else lv_obj_remove_flag(button,LV_OBJ_FLAG_HIDDEN);
    }
  }
  // set_filter rebuilds the old launcher's rows and invokes cover callbacks.
  // Its old visible indices cannot address the newly swapped ROM page.
  visible_indices_.clear();
  focused_rom_ = -1;
  launcher_->set_filter(std::nullopt);
  show_games({});
  launcher_->set_play_enabled(!Tab5Emu::get().usb_msc_host_has_card());
}
