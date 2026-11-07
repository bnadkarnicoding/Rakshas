// game/screenshot.h — saving rendered frames as PNG files.
#pragma once
#include <string>
#include <vector>

#include "core/types.h"

namespace rk {

bool write_png(const std::string& path, const std::vector<u8>& rgba, u32 width, u32 height);

}  // namespace rk
