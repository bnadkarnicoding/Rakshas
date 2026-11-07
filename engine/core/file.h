// core/file.h — whole-file reading helpers.
#pragma once
#include <string>
#include <vector>

#include "core/types.h"

namespace rk {

// Reads an entire binary file. Returns false (and logs) on failure.
bool read_file_bytes(const std::string& path, std::vector<u8>& out);

// Reads a SPIR-V file into 32-bit words. Returns false on failure or bad size.
bool read_spirv(const std::string& path, std::vector<u32>& out);

}  // namespace rk
