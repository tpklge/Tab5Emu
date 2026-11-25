#include "content_storage.hpp"
#include "emu-ui/saves.hpp"
#include "esp_pthread.h"
#include "gui.hpp"
#include "lvgl_private.h"
#include <algorithm>

void Gui::start_cover_worker() {
  auto previous = esp_pthread_get_default_config();
  esp_pthread_get_cfg(&previous);
  auto config = previous;
  config.stack_size = 6144;
  config.thread_name = "cover-worker";
  config.prio = 2;
  config.pin_to_core = 0;
  config.inherit_cfg = false;
  ESP_ERROR_CHECK(esp_pthread_set_cfg(&config));
  cover_thread_ = std::thread([this] {
    for (;;) {
      std::string path, system;
      bool hash = false, index_job = false;
      tab5::library::Query query;
      uint64_t generation = 0;
      std::shared_ptr<CatalogJob> remote;
      {
        std::unique_lock<std::mutex> lk(cover_mutex_);
        cover_cv_.wait(lk, [&] {
          return cover_stop_ ||
                 (!cover_paused_ && (!cover_pending_.empty() ||
                                     !hash_pending_.empty() || library_job_ || catalog_job_));
        });
        if (cover_stop_)
          break;
        if (catalog_job_) {
          remote = std::move(catalog_job_);
          library_cancel_ = false;
        } else if (library_job_) {
          index_job = true;
          query = library_query_;
          generation = library_generation_;
          library_job_ = false;
          library_cancel_ = false;
        } else if (!cover_pending_.empty()) {
          path = cover_pending_.front();
          cover_pending_.pop_front();
        } else {
          path = std::move(hash_pending_);
          system = std::move(hash_system_);
          hash_pending_.clear();
          hash = true;
        }
        cover_busy_ = true;
      }
      if (remote) {
        auto page = tab5::library::read_index(
            tab5::content::resolve("cache/library-v1.t5i"), remote->query,
            [&] { return library_cancel_.load() || remote->cancelled.load(); });
        std::lock_guard<std::mutex> lk(cover_mutex_);
        remote->result = std::move(page);
        remote->done = true;
      } else if (index_job) {
        auto page = tab5::library::read_index(
            tab5::content::resolve("cache/library-v1.t5i"), query, [&] {
              return library_cancel_.load() ||
                     library_generation_.load() != generation;
            });
        if(page.io_error && !library_cancel_ && library_generation_==generation){
          // A read-only retry after remount; keep the worker's SD lease.
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          page=tab5::library::read_index(
              tab5::content::resolve("cache/library-v1.t5i"),query,[&]{
                return library_cancel_.load() || library_generation_.load()!=generation;
              });
        }
        std::lock_guard<std::mutex> lk(cover_mutex_);
        if (generation == library_generation_) {
          if (page.cancelled)
            library_job_ = true;
          else {
            library_result_ = std::move(page);
            library_result_generation_ = generation;
            library_ready_ = true;
          }
        }
      } else if (hash) {
        const auto identity = tab5::content::rom_sha256(path);
        int count = 0;
        if (!identity.empty()) {
          const auto counts = emu_ui::count_saves(save_dir_ + "/" + system +
                                                  "/" + identity + "/core-v1");
          for (const auto &[stem, n] : counts)
            count += n;
        }
        std::lock_guard<std::mutex> lk(cover_mutex_);
        ready_hash_path_ = path;
        ready_hash_ = identity;
        ready_count_ = count;
      } else {
        // File I/O and hardware decode run without LVGL's mutex. Only
        // allocation of the independent draw buffer takes it briefly (in
        // fit_image).
        auto *image = decode_jpeg_hw(path, kWorkerCoverBox);
        std::lock_guard<std::mutex> lk(cover_mutex_);
        cover_ready_.push_back({path, image, 0});
      }
      {
        std::lock_guard<std::mutex> lk(cover_mutex_);
        cover_busy_ = false;
      }
      cover_cv_.notify_all();
    }
  });
  ESP_ERROR_CHECK(esp_pthread_set_cfg(&previous));
}
void Gui::pause_cover_worker(bool pause) {
  std::unique_lock<std::mutex> lk(cover_mutex_);
  cover_paused_ = pause;
  library_cancel_ = pause;
  if (pause)
    cover_cv_.wait(lk, [&] { return !cover_busy_; });
  else
    cover_cv_.notify_all();
}
void Gui::stop_cover_worker() {
  {
    std::lock_guard<std::mutex> lk(cover_mutex_);
    cover_stop_ = true;
    library_cancel_ = true;
  }
  cover_cv_.notify_all();
  if (cover_thread_.joinable())
    cover_thread_.join();
}
const void *Gui::request_cover(const std::string &path, bool priority) {
  if (path.empty())
    return nullptr;
  if (auto it = cover_cache_.find(path); it != cover_cache_.end()) {
    it->second.used = ++cover_tick_;
    return it->second.image;
  }
  std::lock_guard<std::mutex> lk(cover_mutex_);
  if (cover_requested_.insert(path).second) {
    if (cover_pending_.size() < 4) {
      if (priority)
        cover_pending_.push_front(path);
      else
        cover_pending_.push_back(path);
      cover_cv_.notify_all();
    } else
      cover_requested_.erase(path);
  }
  return nullptr;
}
void Gui::request_save_count(size_t index) {
  if (Tab5Emu::get().usb_msc_host_has_card())
    return;
  if (index >= rom_infos_.size())
    return;
  const auto &rom = rom_infos_[index];
  if (save_counts_.contains(rom.rom_path))
    return;
  if (auto known = content_hashes_.find(rom.rom_path);
      known != content_hashes_.end()) {
    int count = 0;
    const auto key = console_info(console_of(rom.platform)).key;
    const auto counts = emu_ui::count_saves(save_dir_ + "/" + key + "/" +
                                            known->second + "/core-v1");
    for (const auto &[stem, n] : counts)
      count += n;
    if (save_counts_.size() >= 128)
      save_counts_.erase(save_counts_.begin());
    save_counts_[rom.rom_path] = count;
    return;
  }
  std::lock_guard<std::mutex> lk(cover_mutex_);
  hash_pending_ = rom.rom_path;
  hash_system_ = console_info(console_of(rom.platform)).key;
  cover_cv_.notify_all();
}
void Gui::accept_cover_results() {
  std::vector<CoverEntry> images;
  std::string path, hash;
  int count = 0;
  {
    std::lock_guard<std::mutex> lk(cover_mutex_);
    images.swap(cover_ready_);
    path = std::move(ready_hash_path_);
    hash = std::move(ready_hash_);
    count = ready_count_;
    ready_hash_path_.clear();
  }
  for (auto &entry : images) {
    entry.used = ++cover_tick_;
    cover_cache_[entry.path] = entry;
  }
  if (!path.empty()) {
    if (save_counts_.size() >= 128)
      save_counts_.erase(save_counts_.begin());
    save_counts_[path] = count;
    record_content_hash(path, hash);
  }
  // A cached image remains valid while LVGL has a reference to it.
  while (cover_cache_.size() > 3) {
    auto victim = cover_cache_.end();
    for (auto it = cover_cache_.begin(); it != cover_cache_.end(); ++it)
      if (it->first != active_cover_path_ && it->first != active_icon_path_ &&
          (victim == cover_cache_.end() ||
           it->second.used < victim->second.used))
        victim = it;
    if (victim == cover_cache_.end())
      break;
    if (victim->second.image) {
      lv_image_cache_drop(victim->second.image);
      lv_draw_buf_destroy(victim->second.image);
    }
    {
      std::lock_guard<std::mutex> lk(cover_mutex_);
      cover_requested_.erase(victim->first);
    }
    cover_cache_.erase(victim);
  }
  if (!path.empty()) {
    const auto selected = get_selected_rom();
    show_games(selected ? selected->rom_path : std::string());
  } else if (!images.empty() && launcher_->selected() >= 0)
    launcher_->select(launcher_->selected());
}
