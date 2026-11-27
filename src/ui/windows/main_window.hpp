// The window: the tools bar (Gestures switch, camera, outputs, OBS, REC, Undo, ⋯), the preview
// with its line underneath, and the side panel (library or editor). Closing it while an output is
// on leaves the app in the tray; otherwise it quits.
#pragma once

#include <QMainWindow>

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QSystemTrayIcon;

namespace racg::app {
class Controller;
}

namespace racg::ui {

class Chip;
class EditorPanel;
class LibraryPanel;
class Preview;
class Switch;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(app::Controller &c, QWidget *parent = nullptr);
  void showAndRaise();
  void quit();

 protected:
  void closeEvent(QCloseEvent *e) override;

 private:
  QWidget *buildTools();
  QWidget *buildLeft();
  void buildTray();
  void refreshStatus();
  void refreshUnder();
  void moreMenu();
  void openSettings();
  void openGesture(const QString &id);
  void showLibrary();

  app::Controller &c_;
  Switch *gestSw_ = nullptr;
  QComboBox *camera_ = nullptr;
  Chip *phoneChip_ = nullptr, *outputs_ = nullptr, *obs_ = nullptr, *rec_ = nullptr;
  QPushButton *undo_ = nullptr, *more_ = nullptr;
  QLabel *banner_ = nullptr, *underLeft_ = nullptr, *underRight_ = nullptr;
  Preview *preview_ = nullptr;
  QStackedWidget *side_ = nullptr;
  LibraryPanel *library_ = nullptr;
  EditorPanel *editor_ = nullptr;
  QSystemTrayIcon *tray_ = nullptr;
  bool quitting_ = false, trayNoted_ = false;
};

}  // namespace racg::ui
