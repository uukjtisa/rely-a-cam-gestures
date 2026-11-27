#include "common/log.hpp"

#include "common/paths.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QString>

#include <cstdio>

namespace racg::log {

namespace {

QMutex mu;
QFile *file = nullptr;

void handler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
  const char *kind = type == QtDebugMsg     ? "debug"
                     : type == QtInfoMsg    ? "info"
                     : type == QtWarningMsg ? "warning"
                                            : "error";
  const QByteArray line =
      (QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + " " + kind + " " + msg + "\n").toUtf8();
  QMutexLocker l(&mu);
  if (file) {
    file->write(line);
    file->flush();
  }
  std::fputs(line.constData(), stderr);
}

}  // namespace

void install() {
  const QString path = paths::logFile();
  if (QFileInfo(path).size() > 1024 * 1024) {
    QFile::remove(path + ".1");
    QFile::rename(path, path + ".1");
  }
  file = new QFile(path);
  if (!file->open(QIODevice::Append | QIODevice::Text)) {
    delete file;
    file = nullptr;
  }
  qInstallMessageHandler(handler);
}

}  // namespace racg::log
