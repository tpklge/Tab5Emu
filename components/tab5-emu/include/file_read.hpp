#pragma once
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>

namespace tab5::content {
enum class ReadStatus { Ok, Missing, IoError, InvalidSize, NoMemory };
struct FreeBytes { void operator()(unsigned char *p) const { std::free(p); } };
struct FileBytes {
  std::unique_ptr<unsigned char, FreeBytes> bytes;
  size_t length{0};
  unsigned char *data() const { return bytes.get(); }
  size_t size() const { return length; }
  unsigned char &operator[](size_t i) const { return data()[i]; }
};
constexpr size_t any_file_size = std::numeric_limits<size_t>::max();
// Consumes/closes file, including on error. Publishes bytes only after the
// complete read, EOF and close succeed. Allocator must be free()-compatible.
ReadStatus read_owned_file(FILE *file, size_t maximum, size_t exact, FileBytes &out,
                          void *(*allocate)(size_t) = std::malloc);
ReadStatus read_file(const std::string &path, size_t maximum, FileBytes &out,
                     size_t exact = any_file_size);
inline bool read_exact_file(const std::string &path, size_t size, FileBytes &out) {
  return read_file(path, size, out, size) == ReadStatus::Ok;
}
} // namespace tab5::content
