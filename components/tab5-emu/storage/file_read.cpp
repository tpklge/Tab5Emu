#include "file_read.hpp"
#include <cerrno>

namespace tab5::content {
ReadStatus read_owned_file(FILE *file, size_t maximum, size_t exact, FileBytes &out,
                          void *(*allocate)(size_t)) {
  if (!file) return ReadStatus::IoError;
  FileBytes staged;
  ReadStatus status = ReadStatus::IoError;
  if (fseek(file, 0, SEEK_END) == 0) {
    const long length = ftell(file);
    if (length >= 0) {
      if (static_cast<unsigned long>(length) > maximum ||
          (exact != any_file_size && static_cast<size_t>(length) != exact)) {
        status = ReadStatus::InvalidSize;
      } else if (fseek(file, 0, SEEK_SET) == 0) {
        staged.length = static_cast<size_t>(length);
        staged.bytes.reset(static_cast<unsigned char *>(allocate(staged.length ? staged.length : 1)));
        if (!staged.bytes) status = ReadStatus::NoMemory;
        else if (fread(staged.data(), 1, staged.size(), file) == staged.size() &&
                 !ferror(file) && fgetc(file) == EOF && !ferror(file))
          status = ReadStatus::Ok;
      }
    }
  }
  if (fclose(file) != 0) status = ReadStatus::IoError;
  if (status == ReadStatus::Ok) out = std::move(staged);
  return status;
}
ReadStatus read_file(const std::string &path, size_t maximum, FileBytes &out, size_t exact) {
  FILE *file = fopen(path.c_str(), "rb");
  if (!file) return errno == ENOENT ? ReadStatus::Missing : ReadStatus::IoError;
  return read_owned_file(file, maximum, exact, out);
}
} // namespace tab5::content
