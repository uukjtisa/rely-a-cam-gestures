#include "check.hpp"

#include <QCoreApplication>

#include <cstring>
#include <exception>

// racg-tests [filter]: runs every test whose name contains `filter`.
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const char *filter = argc > 1 ? argv[1] : "";
  int run = 0;
  for (auto &c : racg::test::cases()) {
    if (!std::strstr(c.name, filter)) continue;
    const int before = racg::test::failures();
    std::printf("%s\n", c.name);
    try {
      c.fn();
    } catch (const std::exception &e) {
      racg::test::fail(c.name, 0, std::string("threw: ") + e.what());
    }
    ++run;
    if (racg::test::failures() == before) std::printf("  ok\n");
  }
  std::printf("\n%d test(s), %d failure(s)\n", run, racg::test::failures());
  return racg::test::failures() ? 1 : 0;
}
