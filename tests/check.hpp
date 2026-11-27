// A small test harness: TEST(name) { CHECK(...); } registers itself; racg-tests runs them all.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace racg::test {

struct Case {
  const char *name;
  std::function<void()> fn;
};
inline std::vector<Case> &cases() {
  static std::vector<Case> c;
  return c;
}
inline int &failures() {
  static int f = 0;
  return f;
}
struct Register {
  Register(const char *n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};

inline void fail(const char *file, int line, const std::string &what) {
  ++failures();
  std::printf("  FAIL %s:%d  %s\n", file, line, what.c_str());
}

}  // namespace racg::test

#define RACG_CAT2(a, b) a##b
#define RACG_CAT(a, b) RACG_CAT2(a, b)
#define TEST(name)                                                                                \
  static void RACG_CAT(test_, name)();                                                            \
  static racg::test::Register RACG_CAT(reg_, name)(#name, RACG_CAT(test_, name));                 \
  static void RACG_CAT(test_, name)()
#define CHECK(cond)                                                                               \
  do {                                                                                            \
    if (!(cond)) racg::test::fail(__FILE__, __LINE__, #cond);                                     \
  } while (0)
#define CHECK_MSG(cond, msg)                                                                      \
  do {                                                                                            \
    if (!(cond)) racg::test::fail(__FILE__, __LINE__, std::string(#cond) + "  (" + (msg) + ")");  \
  } while (0)
