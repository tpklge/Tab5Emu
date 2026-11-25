#include "content_storage.hpp"

// Shared strong definitions remain resident, outside all core overlays.
// Inline/COMDAT copies can otherwise be selected from a different overlay.
namespace tab5::content {
bool atomic_write(const std::string &path,const void *data,size_t size) {
  return atomic_file_write(path,size,[&](FILE *file) {return fwrite(data,1,size,file)==size;});
}
std::string readable_generation(const std::string &path) {
  std::error_code ec;
  if (std::filesystem::is_regular_file(path,ec)) return path;
  if (std::filesystem::is_regular_file(path+".bak",ec)) return path+".bak";
  return {};
}
}
