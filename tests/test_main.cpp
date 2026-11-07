// tests/test_main.cpp — runs every registered test case.
#include <cstdio>

#include "test.h"

namespace rk::test {
std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
int& failures() {
  static int count = 0;
  return count;
}
}  // namespace rk::test

int main() {
  using namespace rk::test;
  for (const Case& c : registry()) {
    int before = failures();
    c.fn();
    std::printf("%s %s\n", failures() == before ? "[pass]" : "[FAIL]", c.name);
  }
  std::printf("%zu tests, %d failed checks\n", registry().size(), failures());
  return failures() == 0 ? 0 : 1;
}
