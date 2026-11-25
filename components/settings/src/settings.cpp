#include "settings.hpp"

#include <tuple>
#include "file_read.hpp"
#include "content_storage.hpp"

#include "format.hpp"

bool AppSettings::same(const Settings &a, const Settings &b) {
  return std::tie(a.volume, a.muted, a.brightness, a.video_setting, a.resume_last_save, a.last_rom, a.last_slot) ==
         std::tie(b.volume, b.muted, b.brightness, b.video_setting, b.resume_last_save, b.last_rom, b.last_slot);
}

bool AppSettings::init(const std::string &file_path, const Settings &defaults) {
  std::lock_guard<std::mutex> lk(mutex_);
  data_ = std::make_unique<Store>(Store::Config{.file_path = file_path, .auto_load = false});
  std::error_code ec;
  const bool loaded=data_->load_or_create(defaults,ec);
  if(!loaded){fmt::print("settings: legacy load failed: {}\n",ec.message());data_->set(defaults);}
  json_path_=(std::filesystem::path(file_path).parent_path()/"settings.json").string();
  tab5::content::FileBytes bytes;
  const auto read = tab5::content::read_file(json_path_, 65536, bytes);
  json_valid_ = read == tab5::content::ReadStatus::Missing;
  if (read == tab5::content::ReadStatus::Ok && bytes.size()) {
    json_base_.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    auto current=data_->get();
    json_valid_=parse_settings_json(json_base_,current,ui_);
    if(json_valid_)data_->set(current);
  }
  if(!json_valid_)fmt::print("settings.json invalid; preserving file and using legacy defaults\n");
  dirty_=false;return loaded && json_valid_;
}

Settings AppSettings::get_settings() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return data_ ? data_->get() : fallback_;
}

bool AppSettings::save() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!dirty_ || !data_) {
    return true;
  }
  std::error_code ec;
  if (!data_->save(ec)) {
    fmt::print("settings: save failed: {}\n", ec.message());
    return false;
  }
  if(json_valid_){
    const auto text=encode_settings_json(json_base_,data_->get(),ui_);
    if(text.empty() || !tab5::content::atomic_write(json_path_,text.data(),text.size()))return false;
    json_base_=text;
  }
  dirty_ = false;
  return true;
}

bool AppSettings::save_if_idle(std::chrono::milliseconds quiet) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!dirty_ || std::chrono::steady_clock::now() - last_change_ < quiet) {
      return true;
    }
  }
  return save();
}

std::optional<int> AppSettings::last_slot(const std::string &rom_path) const {
  std::lock_guard<std::mutex> lk(mutex_);
  const Settings s = data_ ? data_->get() : fallback_;
  if (auto it = s.last_slot.find(rom_path); it != s.last_slot.end()) {
    return it->second;
  }
  return std::nullopt;
}

void AppSettings::set_last_slot(const std::string &rom_path, int slot) {
  modify([&](Settings &s) { s.last_slot[rom_path] = static_cast<uint8_t>(slot); });
}
