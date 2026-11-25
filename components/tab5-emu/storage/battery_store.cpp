#include "battery_store.hpp"
#include "content_paths.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace {
using Digest = std::array<uint8_t, 32>;
using Digests = std::array<Digest, 3>;
bool read_bytes(const std::string &path, size_t size,
                std::vector<uint8_t> &bytes) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec ||
      std::filesystem::file_size(path, ec) != size || ec)
    return false;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return false;
  bytes.resize(size);
  bool ok = fread(bytes.data(), 1, size, f) == size && !ferror(f);
  if (fclose(f))
    ok = false;
  return ok;
}
bool read_digest(const std::string &path, size_t size, Digest &out) {
  std::vector<uint8_t> bytes;
  return read_bytes(path, size, bytes) &&
         tab5::content::digest_bytes(bytes.data(), bytes.size(), out);
}
bool contains(const Digests &hashes, size_t count, const Digest &digest) {
  return std::find(hashes.begin(), hashes.begin() + count, digest) !=
         hashes.begin() + count;
}
void append(Digests &hashes, size_t &count, const Digest &digest) {
  if (count < hashes.size() && !contains(hashes, count, digest))
    hashes[count++] = digest;
}
std::string encode_journal(size_t size, const Digests &hashes, size_t count) {
  std::string text = "T5B1 " + std::to_string(size) + "\n";
  for (size_t n = 0; n < count; ++n) {
    for (uint8_t b : hashes[n]) {
      text += "0123456789abcdef"[b >> 4];
      text += "0123456789abcdef"[b & 15];
    }
    text += '\n';
  }
  return text;
}
bool read_journal(const std::string &path, size_t size, Digests &hashes,
                  size_t &count) {
  std::error_code ec;
  const auto bytes = std::filesystem::file_size(path, ec);
  if (ec || bytes > 256)
    return false;
  std::vector<uint8_t> memory;
  if (!read_bytes(path, bytes, memory))
    return false;
  const std::string text(memory.begin(), memory.end()),
      prefix = "T5B1 " + std::to_string(size) + "\n";
  if (!text.starts_with(prefix) || (text.size() - prefix.size()) % 65)
    return false;
  const auto n = (text.size() - prefix.size()) / 65;
  if (!n || n > 3)
    return false;
  count = 0;
  for (size_t i = 0; i < n; ++i) {
    const auto off = prefix.size() + i * 65;
    if (text[off + 64] != '\n')
      return false;
    Digest digest{};
    for (size_t b = 0; b < 32; ++b) {
      auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9')
          return c - '0';
        if (c >= 'a' && c <= 'f')
          return c - 'a' + 10;
        return -1;
      };
      int a = digit(text[off + b * 2]), z = digit(text[off + b * 2 + 1]);
      if (a < 0 || z < 0)
        return false;
      digest[b] = (a << 4) | z;
    }
    if (contains(hashes, count, digest))
      return false;
    append(hashes, count, digest);
  }
  return true;
}
bool quarantine(const std::string &path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return !ec;
  const auto rejected = path + ".rejected";
  return !ec && !std::filesystem::exists(rejected, ec) && !ec &&
         rename(path.c_str(), rejected.c_str()) == 0;
}
} // namespace

namespace tab5::content {
BatteryStore::BatteryStore() = default;
BatteryStore::~BatteryStore() = default;
BatteryStore &primary_battery_store() {
  static BatteryStore store;
  return store;
}
BatteryStore &rtc_battery_store() {
  static BatteryStore store;
  return store;
}
void BatteryStore::reset() {
  std::string().swap(path_);
  size_ = 0;
  ready_ = digest_valid_ = from_backup_ = false;
  accepted_count_ = 0;
  journal_seen_ = journal_repair_ = false;
}
bool BatteryStore::configure(std::string_view system, std::string_view hash,
                             size_t bytes, std::string_view filename) {
  reset();
  if (!bytes) {
    ready_ = true;
    return true;
  }
  if (bytes > 1024 * 1024 || hash.size() != 64)
    return false;
  for (char c : hash)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  if (system.empty() || system.find('/') != system.npos ||
      !valid_relative(system))
    return false;
  if (filename.empty() || filename.find('/') != filename.npos ||
      !valid_relative(filename))
    return false;
  path_ = resolve("saves/" + std::string(system) + "/" + std::string(hash) +
                  "/" + std::string(filename));
  if (path_.empty())
    return false;
  size_ = bytes;
  return true;
}
bool BatteryStore::load(void *data) {
  if (path_.empty())
    return ready_ && size_ == 0;
  if (!data)
    return false;
  const auto journal = path_ + ".sha256";
  bool journal_valid = false;
  for (bool backup : {false, true}) {
    const auto candidate = journal + (backup ? ".bak" : "");
    std::error_code ec;
    const bool exists = std::filesystem::exists(candidate, ec);
    if (ec)
      return false;
    if (!exists)
      continue;
    journal_seen_ = true;
    if (read_journal(candidate, size_, accepted_, accepted_count_)) {
      journal_valid = true;
      journal_repair_ = backup;
      break;
    }
  }
  // An invalid existing journal is not an invitation to import legacy data.
  if (journal_seen_ && !journal_valid)
    return false;
  bool found = false;
  for (bool backup : {false, true}) {
    const auto file_path = path_ + (backup ? ".bak" : "");
    std::error_code ec;
    const bool exists = std::filesystem::exists(file_path, ec);
    if (ec)
      return false;
    if (!exists)
      continue;
    found = true;
    std::vector<uint8_t> staged;
    Digest digest{};
    if (!read_bytes(file_path, size_, staged) ||
        !digest_bytes(staged.data(), size_, digest))
      continue;
    if (journal_seen_ && !contains(accepted_, accepted_count_, digest))
      continue;
    memcpy(data, staged.data(), size_);
    from_backup_ = backup;
    digest_ = digest;
    digest_valid_ = true;
    ready_ = true;
    return true;
  }
  // Existing invalid data is preserved; caller must refuse to start/save.
  ready_ = !found && !journal_seen_;
  return ready_;
}
bool BatteryStore::flush(const void *data) {
  if (!ready_)
    return false;
  if (path_.empty())
    return size_ == 0;
  if (!data)
    return false;
  std::array<uint8_t, 32> digest{};
  if (!digest_bytes(data, size_, digest))
    return false;
  std::error_code ec;
  Digest primary{}, backup{};
  const bool primary_read = read_digest(path_, size_, primary);
  const bool primary_valid =
      primary_read && !from_backup_ && (!digest_valid_ || primary == digest_) &&
      (!journal_seen_ || contains(accepted_, accepted_count_, primary));
  const bool backup_valid =
      read_digest(path_ + ".bak", size_, backup) &&
      (!journal_seen_ || contains(accepted_, accepted_count_, backup));
  if (primary_valid && digest_valid_ && digest == digest_ && journal_seen_ &&
      !journal_repair_)
    return true;
  Digests next{};
  size_t count = 0;
  append(next, count, digest);
  if (primary_valid)
    append(next, count, primary);
  if (backup_valid)
    append(next, count, backup);
  const auto journal = path_ + ".sha256";
  // A journal recovered from its backup must not rotate the invalid primary
  // over that only valid copy. Keep it as evidence before publishing.
  if (journal_repair_ && !quarantine(journal))
    return false;
  const auto text = encode_journal(size_, next, count);
  // Write-ahead: interruption here still leaves hashes for the old primary
  // and backup; interruption after data commit also validates the new bytes.
  if (!atomic_write(journal, text.data(), text.size()))
    return false;
  accepted_ = next;
  accepted_count_ = count;
  journal_seen_ = true;
  journal_repair_ = false;
  if (primary_valid && primary == digest) {
    digest_ = digest;
    digest_valid_ = true;
    return true;
  }
  // Never rotate an invalid primary over the last valid raw backup.
  if (!primary_valid && !quarantine(path_))
    return false;
  if (ec || !atomic_write(path_, data, size_))
    return false;
  digest_ = digest;
  digest_valid_ = true;
  from_backup_ = false;
  return true;
}
} // namespace tab5::content
