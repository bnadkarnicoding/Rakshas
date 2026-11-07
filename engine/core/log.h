// core/log.h — minimal printf-style logging and asserts.
// Errors are counted so headless test runs can fail if anything went wrong.
#pragma once
#include "core/types.h"

namespace rk {

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);

// Number of log_error calls since start-up.
u32 log_error_count();

[[noreturn]] void fatal(const char* file, int line, const char* expr);

}  // namespace rk

// RK_ASSERT checks programmer mistakes; it is active in every build type.
#define RK_ASSERT(expr) \
  do { if (!(expr)) ::rk::fatal(__FILE__, __LINE__, #expr); } while (0)

// RK_FATAL stops the program with a message (for states that must never happen).
#define RK_FATAL(msg) ::rk::fatal(__FILE__, __LINE__, msg)
