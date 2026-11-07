// game/screenshot.cpp — PNG writing via stb_image_write (approved library).
#include "screenshot.h"

#include "core/log.h"

// Third-party code is compiled without our strict warning set.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace rk {

bool write_png(const std::string& path, const std::vector<u8>& rgba, u32 width, u32 height) {
  if (rgba.size() < static_cast<size_t>(width) * height * 4) {
    log_error("write_png: image data too small");
    return false;
  }
  int ok = stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4,
                          rgba.data(), static_cast<int>(width * 4));
  if (!ok) {
    log_error("write_png: could not write %s", path.c_str());
    return false;
  }
  return true;
}

}  // namespace rk
