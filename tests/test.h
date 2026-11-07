// tests/test.h — a tiny self-contained test harness (no external library).
#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace rk::test {

struct Case {
  const char* name;
  std::function<void()> fn;
};

std::vector<Case>& registry();
int& failures();

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

}  // namespace rk::test

#define RK_TEST_CAT2(a, b) a##b
#define RK_TEST_CAT(a, b) RK_TEST_CAT2(a, b)
#define TEST(name)                                                              \
  static void name();                                                           \
  static ::rk::test::Registrar RK_TEST_CAT(reg_, name)(#name, name);            \
  static void name()

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ++::rk::test::failures();                                                 \
    }                                                                           \
  } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) <= (eps))
