// core/file.cpp — whole-file reading helpers.
#include "core/file.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace rk {

bool read_file_bytes(const std::string& path, std::vector<u8>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    log_error("could not open file: %s", path.c_str());
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(f);
    log_error("could not size file: %s", path.c_str());
    return false;
  }
  out.resize(static_cast<size_t>(size));
  size_t got = size > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
  std::fclose(f);
  if (got != out.size()) {
    log_error("short read on file: %s", path.c_str());
    return false;
  }
  return true;
}

bool read_spirv(const std::string& path, std::vector<u32>& out) {
  std::vector<u8> bytes;
  if (!read_file_bytes(path, bytes)) return false;
  if (bytes.empty() || bytes.size() % 4 != 0) {
    log_error("not a SPIR-V file (size %zu): %s", bytes.size(), path.c_str());
    return false;
  }
  out.resize(bytes.size() / 4);
  std::memcpy(out.data(), bytes.data(), bytes.size());
  return true;
}

}  // namespace rk
