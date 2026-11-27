// Rely-A-Cam-Gestures. One copy runs per Windows user: a second launch just brings the first one's
// window up. `--hidden` starts in the tray (Start with Windows uses it).
#include "app/controller.hpp"
#include "common/log.hpp"
#include "common/paths.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/kit.hpp"
#include "ui/windows/main_window.hpp"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStyleFactory>

#include <thread>

#include <windows.h>

namespace {

// How long Quit may take before the process is ended anyway.
constexpr DWORD kQuitGraceMs = 4000;

QString instanceName() {
  wchar_t user[256] = {};
  DWORD n = 256;
  GetUserNameW(user, &n);
  return QString("%1-%2").arg(racg::paths::kAppName, QString::fromWCharArray(user));
}

// True if another copy is running (and was asked to show itself).
bool alreadyRunning(const QString &name) {
  QLocalSocket s;
  s.connectToServer(name);
  if (!s.waitForConnected(300)) return false;
  s.write("show\n");
  s.waitForBytesWritten(300);
  return true;
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
  QApplication app(argc, argv);
  QApplication::setApplicationName(racg::paths::kAppName);
  QApplication::setApplicationVersion(RACG_VERSION);
  QApplication::setOrganizationName("Niccc2007");
  QApplication::setQuitOnLastWindowClosed(false);   // the tray keeps it running while an output is on
  racg::ui::keepMenusOpenOnToggle(&app);

  // Dev: --shot DIR renders the window off screen, one picture per side tab, and quits. It reads the
  // library and settings but never starts a camera, the link or a save.
  if (const qsizetype i = QApplication::arguments().indexOf("--shot"); i > 0 && i + 1 < QApplication::arguments().size()) {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    app.setStyleSheet(racg::ui::theme::styleSheet());
    racg::app::Controller controller;
    racg::ui::MainWindow window(controller);
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.resize(1356, 733);
    window.show();
    window.shoot(QApplication::arguments().at(i + 1));
    TerminateProcess(GetCurrentProcess(), 0);   // no shutdown work: nothing was started
  }

  const QString name = instanceName();
  if (alreadyRunning(name)) return 0;
  QLocalServer::removeServer(name);   // a stale one from a crash
  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption);
  server.listen(name);

  racg::log::install();
  qInfo() << "start" << RACG_VERSION << QApplication::arguments();

  QApplication::setStyle(QStyleFactory::create("Fusion"));
  app.setStyleSheet(racg::ui::theme::styleSheet());

  racg::app::Controller controller;
  racg::ui::MainWindow window(controller);
  QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
    while (QLocalSocket *s = server.nextPendingConnection()) {
      QObject::connect(s, &QLocalSocket::readyRead, &window, [s, &window] {
        if (s->readAll().startsWith("show")) window.showAndRaise();
        s->deleteLater();
      });
    }
  });
  QObject::connect(&app, &QApplication::aboutToQuit, &controller, [&] {
    // Quit always quits: a driver or device that won't let go (a camera read, an audio endpoint)
    // must never leave a dead icon in the tray. Settings are saved on every edit, so nothing is lost.
    std::thread([] {
      Sleep(kQuitGraceMs);
      TerminateProcess(GetCurrentProcess(), 0);
    }).detach();
    controller.shutdown();
  });
  // Started by Rely-A-Cam's plugin: hidden, and gone again when the plugin lets go (unless the
  // window was opened meanwhile).
  controller.setAttachLaunch(QApplication::arguments().contains("--attach"));
  controller.windowVisible = [&window] { return window.isVisible(); };
  QObject::connect(&controller, &racg::app::Controller::showWindow, &window, &racg::ui::MainWindow::showAndRaise);

  if (const qsizetype i = QApplication::arguments().indexOf("--still"); i > 0 && i + 1 < QApplication::arguments().size())
    controller.setStill(QApplication::arguments().at(i + 1));
  controller.start();
  if (!QApplication::arguments().contains("--hidden") && !QApplication::arguments().contains("--attach"))
    window.show();
  return app.exec();
}
