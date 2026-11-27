#include "common/autostart.hpp"

#include "common/paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace racg::autostart {

namespace {
constexpr const char *kRunKey = R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)";

QString command() {
  return "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" --hidden";
}
}  // namespace

bool enabled() {
  QSettings run(kRunKey, QSettings::NativeFormat);
  return run.contains(paths::kAppName);
}

void set(bool on) {
  QSettings run(kRunKey, QSettings::NativeFormat);
  if (on) run.setValue(paths::kAppName, command());
  else run.remove(paths::kAppName);
}

}  // namespace racg::autostart
