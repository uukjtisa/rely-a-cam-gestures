#include "ui/windows/main_window.hpp"

#include "app/controller.hpp"
#include "common/paths.hpp"
#include "ui/dialogs/settings_dialog.hpp"
#include "ui/panels/editor_panel.hpp"
#include "ui/panels/library_panel.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/kit.hpp"
#include "ui/widgets/preview.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace racg::ui {

using namespace theme;

MainWindow::MainWindow(app::Controller &c, QWidget *parent) : QMainWindow(parent), c_(c) {
  setWindowTitle(paths::kAppName);
  setWindowIcon(QIcon(":/icon.svg"));
  auto *root = new QWidget(this);
  root->setObjectName("root");
  auto *v = new QVBoxLayout(root);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  v->addWidget(buildTools());

  auto *body = new QHBoxLayout;
  body->setSpacing(0);
  body->addWidget(buildLeft(), 1);

  side_ = new QStackedWidget(root);
  side_->setObjectName("side");
  side_->setFixedWidth(sideWidth);
  side_->setAttribute(Qt::WA_StyledBackground);
  library_ = new LibraryPanel(c_, side_);
  side_->addWidget(library_);
  auto *edScroll = new QScrollArea(side_);
  edScroll->setWidgetResizable(true);
  edScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor_ = new EditorPanel(c_, edScroll);
  edScroll->setWidget(editor_);
  side_->addWidget(edScroll);
  body->addWidget(side_);
  v->addLayout(body, 1);
  setCentralWidget(root);
  resize(1160, 700);

  connect(library_, &LibraryPanel::openGesture, this, &MainWindow::openGesture);
  connect(library_, &LibraryPanel::recordNew, this, [this] {
    const QString id = c_.newGesture();
    openGesture(id);
    c_.recordTake(id);
  });
  connect(library_, &LibraryPanel::addMemes, this, [this] {
    const QStringList files = QFileDialog::getOpenFileNames(this, "Add memes", {}, memes::Library::fileFilters().join(";;"));
    if (!files.isEmpty()) c_.addMemes(files);
  });
  connect(library_, &LibraryPanel::importItsgiving, this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(this, "Your itsgiving folder (the one with assets inside)");
    if (!dir.isEmpty()) c_.importItsgiving(dir);
  });
  connect(library_, &LibraryPanel::spotMeme, preview_, &Preview::setSpotMeme);
  connect(editor_, &EditorPanel::back, this, &MainWindow::showLibrary);
  connect(&c_, &app::Controller::statusChanged, this, &MainWindow::refreshStatus);
  connect(&c_, &app::Controller::observed, this, &MainWindow::refreshUnder);
  connect(&c_, &app::Controller::libraryChanged, this, [this] {
    // The gesture being edited was deleted elsewhere: back to the library.
    if (side_->currentIndex() == 1 && !c_.gestures().find(editor_->gestureId())) showLibrary();
  });

  auto *undo = new QShortcut(QKeySequence::Undo, this);
  connect(undo, &QShortcut::activated, this, [this] { c_.undo(); });
  buildTray();
  refreshStatus();
  refreshUnder();
}

QWidget *MainWindow::buildTools() {
  auto *bar = new QWidget(this);
  bar->setObjectName("tools");
  bar->setAttribute(Qt::WA_StyledBackground);
  auto *h = new QHBoxLayout(bar);
  h->setContentsMargins(10, 6, 10, 6);
  h->setSpacing(8);

  gestSw_ = new Switch("Gestures on", bar);
  connect(gestSw_, &Switch::toggled, this, [this](bool on) { c_.setGesturesOn(on); });
  h->addWidget(gestSw_);
  auto *camL = note("Camera", bar);
  camL->setContentsMargins(6, 0, 0, 0);
  h->addWidget(camL);
  camera_ = new QComboBox(bar);
  camera_->setMinimumWidth(150);
  camera_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  connect(camera_, &QComboBox::activated, this, [this] {
    const QString id = camera_->currentData().toString();
    if (id == "refresh") {
      c_.refreshCameras();
      return;
    }
    c_.setCamera(id);
  });
  h->addWidget(camera_);
  phoneChip_ = new Chip({}, Chip::Ok, bar);
  phoneChip_->setToolTip("The phone this window shows. Click to pick another phone with gestures on.");
  connect(phoneChip_, &Chip::clicked, this, [this] {
    QMenu m(this);
    for (const auto &[dev, name] : c_.phones()) {
      QAction *a = m.addAction(name.isEmpty() ? dev : name);
      a->setCheckable(true);
      a->setChecked(dev == c_.shownPhone());
      connect(a, &QAction::triggered, this, [this, d = dev] { c_.showPhone(d); });
    }
    m.exec(phoneChip_->mapToGlobal(QPoint(0, phoneChip_->height() + 2)));
  });
  h->addWidget(phoneChip_);
  outputs_ = new Chip({}, Chip::Ok, bar);
  outputs_->setToolTip("Settings > Outputs");
  outputs_->setCursor(Qt::PointingHandCursor);
  connect(outputs_, &Chip::clicked, this, &MainWindow::openSettings);
  h->addWidget(outputs_);
  obs_ = new Chip("OBS found", Chip::Ok, bar);
  h->addWidget(obs_);
  rec_ = new Chip("● REC (OBS)", Chip::Rec, bar);
  h->addWidget(rec_);
  h->addStretch(1);
  undo_ = new QPushButton("Undo", bar);
  undo_->setToolTip("Undo the last gesture's action (Ctrl+Z)");
  connect(undo_, &QPushButton::clicked, this, [this] { c_.undo(); });
  h->addWidget(undo_);
  more_ = new QPushButton("⋯", bar);
  more_->setToolTip("Overlays, calibrate, import, settings");
  connect(more_, &QPushButton::clicked, this, &MainWindow::moreMenu);
  h->addWidget(more_);
  return bar;
}

QWidget *MainWindow::buildLeft() {
  auto *left = new QWidget(this);
  auto *v = new QVBoxLayout(left);
  v->setContentsMargins(10, 10, 10, 10);
  v->setSpacing(8);
  banner_ = new QLabel("Attached to Rely-A-Cam in OBS: actions run on the phone and in OBS. Closing this window keeps gestures running.", left);
  banner_->setObjectName("banner");
  banner_->setWordWrap(true);
  v->addWidget(banner_);
  preview_ = new Preview(c_, left);
  v->addWidget(preview_);
  auto *under = new QHBoxLayout;
  under->setSpacing(8);
  underLeft_ = new QLabel(left);
  underLeft_->setObjectName("under");
  underRight_ = new QLabel(left);
  underRight_->setObjectName("under");
  under->addWidget(underLeft_, 1);
  under->addWidget(underRight_);
  v->addLayout(under);
  v->addStretch(1);
  return left;
}

void MainWindow::buildTray() {
  tray_ = new QSystemTrayIcon(QIcon(":/icon.svg"), this);
  tray_->setToolTip(paths::kAppName);
  auto *menu = new QMenu(this);
  menu->addAction("Open Rely-A-Cam-Gestures", this, &MainWindow::showAndRaise);
  QAction *on = menu->addAction("Gestures on");
  on->setCheckable(true);
  connect(on, &QAction::toggled, this, [this](bool v) {
    if (v != c_.settings().gesturesOn) c_.setGesturesOn(v);
  });
  connect(menu, &QMenu::aboutToShow, this, [this, on] {
    QSignalBlocker b(on);
    on->setChecked(c_.settings().gesturesOn);
  });
  menu->addSeparator();
  menu->addAction("Quit", this, &MainWindow::quit);
  tray_->setContextMenu(menu);
  connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
    if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) showAndRaise();
  });
  tray_->show();
}

void MainWindow::showAndRaise() {
  if (isMinimized()) showNormal();
  show();
  raise();
  activateWindow();
}

void MainWindow::quit() {
  quitting_ = true;
  close();
  QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *e) {
  const Settings &s = c_.settings();
  const bool outputOn = s.outVirtualCamera || s.outObsCamera;
  if (!quitting_ && outputOn && QSystemTrayIcon::isSystemTrayAvailable()) {
    // An output is on: viewers still see the camera, so keep running in the tray.
    hide();
    e->ignore();
    if (!trayNoted_) {
      trayNoted_ = true;
      tray_->showMessage(paths::kAppName, "Still running in the tray, so the virtual camera stays on. Quit from the tray menu.",
                         QIcon(":/icon.svg"), 4000);
    }
    return;
  }
  e->accept();
  if (!quitting_) {
    quitting_ = true;
    QApplication::quit();
  }
}

void MainWindow::refreshStatus() {
  const auto s = c_.situation();
  const bool rely = s == actions::Situation::Rely;
  {
    QSignalBlocker b(gestSw_);
    gestSw_->setChecked(c_.settings().gesturesOn);
  }
  gestSw_->setText(c_.settings().gesturesOn ? "Gestures on" : "Gestures paused");
  gestSw_->updateGeometry();

  camera_->setVisible(!rely);
  phoneChip_->setVisible(rely);
  if (!rely) {
    QSignalBlocker b(camera_);
    camera_->clear();
    for (const auto &cam : c_.cameras()) camera_->addItem(cam.name, cam.id);
    if (c_.cameras().isEmpty()) camera_->addItem("No camera found", QString());
    camera_->insertSeparator(camera_->count());
    camera_->addItem("Look for cameras again", "refresh");
    const int i = camera_->findData(c_.settings().cameraId);
    camera_->setCurrentIndex(i >= 0 ? i : 0);
  } else {
    const int n = int(c_.phones().size());
    phoneChip_->setText("Phone: " + c_.cameraName() + " (through Rely-A-Cam)" + (n > 1 ? QString(" ▾ %1 phones").arg(n) : QString()));
  }
  outputs_->setVisible(!rely);
  outputs_->setText(c_.outputsLabel());
  outputs_->setKind(c_.outputsLabel() == "No outputs" ? Chip::Plain : Chip::Ok);
  obs_->setVisible(s == actions::Situation::OwnObs);
  rec_->setVisible(c_.obsRecording());
  const bool can = c_.runner().canUndo();
  undo_->setEnabled(can);
  undo_->setText(can ? "Undo: " + c_.runner().undoLabel() : QString("Undo"));
  banner_->setVisible(rely);
  setWindowTitle(rely ? QString("Rely-A-Cam-Gestures: editing for OBS, phone %1").arg(c_.cameraName()) : QString(paths::kAppName));
  refreshUnder();
}

void MainWindow::refreshUnder() {
  const Settings &s = c_.settings();
  QStringList extra;
  if (s.grid.preview && !s.grid.output) extra << "the grid";
  if (s.detection.preview && !s.detection.output) extra << "the detection overlay";
  QString t = "Preview shows what the output gets";
  if (!extra.isEmpty()) t += ", plus " + extra.join(" and ") + " (preview only)";
  t += ".";
  if (underLeft_->text() != t) underLeft_->setText(t);
  const QString r = c_.detectorInfo();
  if (underRight_->text() != r) underRight_->setText(r);
}

void MainWindow::moreMenu() {
  QMenu m(this);
  QMenu *ov = m.addMenu("Overlays");
  auto places = [this](QMenu *sub, Settings::Places Settings::*field) {
    for (int i = 0; i < 2; ++i) {
      QAction *a = sub->addAction(i == 0 ? "Preview" : "Output");
      a->setCheckable(true);
      a->setChecked(i == 0 ? (c_.settings().*field).preview : (c_.settings().*field).output);
      connect(a, &QAction::toggled, this, [this, field, i](bool on) {
        (i == 0 ? (c_.settings().*field).preview : (c_.settings().*field).output) = on;
        c_.settingsEdited();
      });
    }
  };
  places(ov->addMenu("Grid"), &Settings::grid);
  places(ov->addMenu("Detection"), &Settings::detection);
  m.addAction("Calibrate neutral face...", this, [this] { c_.calibrate(); });
  m.addAction("Import from itsgiving folder...", this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(this, "Your itsgiving folder (the one with assets inside)");
    if (!dir.isEmpty()) c_.importItsgiving(dir);
  });
  m.addSeparator();
  m.addAction("Settings...", this, &MainWindow::openSettings);
  m.addAction("About", this, [this] {
    QMessageBox::about(
        this, "About Rely-A-Cam-Gestures",
        QString("<b>Rely-A-Cam-Gestures %1</b><br>Gestures that run memes, camera moves and OBS actions.<br><br>"
                "By Niccc2007. Free software under the GNU GPL v3.<br><br>"
                "The 14 reactions are ported from <b>itsgiving</b> (MIT).<br>"
                "Hand, face and body models: Google MediaPipe (Apache 2.0).<br>"
                "Built with Qt (LGPL v3), ONNX Runtime (MIT) and FFmpeg (LGPL).")
            .arg(QApplication::applicationVersion()));
  });
  m.exec(more_->mapToGlobal(QPoint(more_->width() - m.sizeHint().width(), more_->height() + 2)));
}

void MainWindow::openSettings() {
  SettingsDialog d(c_, this);
  connect(&d, &SettingsDialog::calibrate, this, [this] { c_.calibrate(); });
  d.exec();
  refreshStatus();
}

void MainWindow::openGesture(const QString &id) {
  preview_->setSpotMeme({});
  editor_->open(id);
  side_->setCurrentIndex(1);
}

void MainWindow::showLibrary() {
  side_->setCurrentIndex(0);
  library_->rebuild();
}

}  // namespace racg::ui
