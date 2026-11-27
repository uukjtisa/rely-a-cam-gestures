#include "common/paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace racg::paths {

namespace {
QString made(const QString &dir) {
  QDir().mkpath(dir);
  return dir;
}
}  // namespace

QString dataDir() {
  // RACG_DATA_DIR keeps developer runs (screenshots, experiments) out of your real library.
  if (const QString dev = qEnvironmentVariable("RACG_DATA_DIR"); !dev.isEmpty()) return made(dev);
  QString base = qEnvironmentVariable("APPDATA");
  if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return made(QDir(base).filePath(kAppName));
}

QString libraryDir() { return made(QDir(dataDir()).filePath("library")); }
QString mediaDir() { return made(QDir(libraryDir()).filePath("media")); }
QString takesDir() { return made(QDir(libraryDir()).filePath("takes")); }
QString gesturesFile() { return QDir(libraryDir()).filePath("gestures.json"); }
QString memesFile() { return QDir(libraryDir()).filePath("memes.json"); }
QString calibrationFile() { return QDir(dataDir()).filePath("calibration.json"); }
QString settingsFile() { return QDir(dataDir()).filePath("settings.json"); }
QString logFile() { return QDir(dataDir()).filePath("log.txt"); }

QString snapshotsDir() {
  return made(QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(kAppName));
}

QString modelsDir() {
  const QString beside = QDir(QCoreApplication::applicationDirPath()).filePath("models");
  if (QDir(beside).exists()) return beside;
#ifdef RACG_DEV_MODELS
  return RACG_DEV_MODELS;
#else
  return beside;
#endif
}

}  // namespace racg::paths
