// core/log.cpp — logging implementation (stdout/stderr for now).
#include "core/log.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace rk {
namespace {
std::atomic<u32> g_error_count{0};

void vlog(FILE* out, const char* tag, const char* fmt, va_list args) {
  std::fprintf(out, "[%s] ", tag);
  std::vfprintf(out, fmt, args);
  std::fputc('\n', out);
  std::fflush(out);
}
}  // namespace

void log_info(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog(stdout, "info", fmt, args);
  va_end(args);
}

void log_warn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlog(stderr, "warn", fmt, args);
  va_end(args);
}

void log_error(const char* fmt, ...) {
  g_error_count.fetch_add(1);
  va_list args;
  va_start(args, fmt);
  vlog(stderr, "error", fmt, args);
  va_end(args);
}

u32 log_error_count() { return g_error_count.load(); }

void fatal(const char* file, int line, const char* expr) {
  std::fprintf(stderr, "[fatal] %s:%d: assertion failed: %s\n", file, line, expr);
  std::fflush(stderr);
  std::abort();
}

}  // namespace rk
