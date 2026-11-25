#include "battery_store.hpp"
#include "content_paths.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>

// Host substitutes a deterministic fingerprint for the SDK-only PSA call.
// These tests cover file transactions/dedup, not SHA-256 correctness. Firmware
// uses PSA SHA-256 and its physical readbacks are verified separately.
bool tab5::content::digest_bytes(const void *data, size_t size,
                                 std::array<uint8_t, 32> &out) {
  const auto hash =
      std::hash<std::string_view>{}({static_cast<const char *>(data), size});
  for (unsigned i = 0; i < 32; ++i)
    out[i] = static_cast<uint64_t>(hash) >> ((i % 8) * 8);
  return true;
}
int main() {
  using namespace tab5::content;
  namespace fs = std::filesystem;
  fs::path root(tab5::content::root);
  assert(fs::create_directory(root));
  const std::string hash(64, 'a');
  auto read = [](const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
  };
  BatteryStore store;
  assert(!store.configure("../gb", hash, 4));
  assert(!store.configure("gb", "bad-hash", 4));
  assert(!store.configure("gb", hash, 1024 * 1024 + 1));
  assert(store.configure("gb", hash, 4));
  char memory[4] = {'1', '1', '1', '1'};
  assert(store.load(memory));
  assert(store.flush(memory));
  const auto path = store.path();
  assert(read(path) == "1111" && !fs::exists(path + ".bak"));
  memcpy(memory, "2222", 4);
  assert(store.flush(memory));
  assert(read(path + ".bak") == "1111");
  assert(store.flush(memory));
  assert(read(path + ".bak") == "1111");
  store.reset();
  assert(store.configure("gb", hash, 4));
  memcpy(memory, "0000", 4);
  assert(store.load(memory) && memcmp(memory, "2222", 4) == 0);
  assert(store.flush(memory) && read(path + ".bak") == "1111");
  std::ofstream(path, std::ios::binary | std::ios::trunc) << "X";
  store.reset();
  assert(store.configure("gb", hash, 4));
  assert(store.load(memory) && memcmp(memory, "1111", 4) == 0);
  assert(store.flush(memory));
  assert(read(path) == "1111" && read(path + ".bak") == "1111" &&
         read(path + ".rejected") == "X");
  assert(!atomic_file_write(path, 4, [](FILE *f) {
    fwrite("bad", 1, 3, f);
    return false;
  }));
  assert(read(path) == "1111" && read(path + ".bak") == "1111");
  // Invalid generations cannot overwrite an emulated default RAM image.
  std::ofstream(path, std::ios::binary | std::ios::trunc) << "broken";
  std::ofstream(path + ".bak", std::ios::binary | std::ios::trunc) << "broken";
  store.reset();
  assert(store.configure("gb", hash, 4));
  memcpy(memory, "0000", 4);
  assert(!store.load(memory) && memcmp(memory, "0000", 4) == 0 &&
         !store.flush(memory));
  assert(read(path) == "broken" && read(path + ".bak") == "broken");
  store.reset();
  assert(store.configure("gb", {}, 0));
  assert(store.load(nullptr) && store.flush(nullptr));
  auto start = [&](char identity) {
    store.reset();
    assert(store.configure("gb", std::string(64, identity), 4));
    memcpy(memory, "aaaa", 4);
    assert(store.load(memory));
    assert(store.flush(memory));
  };
  auto reopen = [&](char identity) {
    store.reset();
    assert(store.configure("gb", std::string(64, identity), 4));
    memcpy(memory, "0000", 4);
    return store.load(memory);
  };
  auto write = [](const std::string &p, const std::string &bytes) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << bytes;
  };
  // Equal-size corruption must fail integrity and recover the old generation.
  start('b');
  const auto checked = store.path();
  memcpy(memory, "bbbb", 4);
  assert(store.flush(memory));
  write(checked, "zzzz");
  assert(reopen('b') && memcmp(memory, "aaaa", 4) == 0);
  assert(store.flush(memory));
  assert(read(checked + ".rejected") == "zzzz");
  assert(read(checked) == "aaaa" && read(checked + ".bak") == "aaaa");
  // Both equal-size generations invalid: no default RAM overwrite.
  write(checked, "xxxx");
  write(checked + ".bak", "yyyy");
  assert(!reopen('b') && memcmp(memory, "0000", 4) == 0 &&
         !store.flush(memory));
  // Legacy files stay raw and gain a journal without rotating a duplicate.
  store.reset();
  assert(store.configure("gb", std::string(64, 'c'), 4));
  const auto legacy = store.path();
  fs::create_directories(fs::path(legacy).parent_path());
  write(legacy, "old!");
  assert(store.load(memory) && memcmp(memory, "old!", 4) == 0 &&
         store.flush(memory));
  assert(read(legacy) == "old!" && !fs::exists(legacy + ".bak") &&
         read(legacy + ".sha256").starts_with("T5B1 4\n"));
  // The write-ahead journal accepts the old files if data publication is
  // interrupted, including the window after primary -> backup rename.
  start('d');
  const auto pending = store.path();
  memcpy(memory, "bbbb", 4);
  assert(store.flush(memory));
  memcpy(memory, "cccc", 4);
  assert(store.flush(memory));
  write(pending, "bbbb");
  write(pending + ".bak", "aaaa");
  assert(reopen('d') && memcmp(memory, "bbbb", 4) == 0);
  assert(store.flush(memory));
  fs::remove(pending);
  write(pending + ".bak", "bbbb");
  assert(reopen('d') && memcmp(memory, "bbbb", 4) == 0 && store.flush(memory));
  // Damaged journal: recover its backup, then the raw generation it attests.
  start('e');
  const auto meta = store.path();
  memcpy(memory, "bbbb", 4);
  assert(store.flush(memory));
  memcpy(memory, "cccc", 4);
  assert(store.flush(memory));
  write(meta + ".sha256", "bad");
  assert(reopen('e') && memcmp(memory, "bbbb", 4) == 0 && store.flush(memory));
  assert(read(meta + ".sha256.rejected") == "bad" &&
         read(meta + ".rejected") == "cccc");
  // Metadata write failure leaves both raw generations intact.
  start('f');
  const auto failed = store.path();
  memcpy(memory, "bbbb", 4);
  assert(store.flush(memory));
  assert(fs::create_directory(failed + ".sha256.tmp"));
  memcpy(memory, "cccc", 4);
  assert(!store.flush(memory));
  assert(read(failed) == "bbbb" && read(failed + ".bak") == "aaaa");
  fs::remove(failed + ".sha256.tmp");
  write(failed + ".sha256", "bad");
  write(failed + ".sha256.bak", "bad");
  assert(!reopen('f') && memcmp(memory, "0000", 4) == 0 &&
         !store.flush(memory));
  fs::remove_all(root);
  std::cout << "battery store: generations, raw legacy migration, SHA journal, "
               "equal-size corruption, interrupted publication, metadata "
               "recovery, rejected data and failed writers OK\n";
}
