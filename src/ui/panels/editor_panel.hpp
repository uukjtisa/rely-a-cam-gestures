// The side panel's second page: one gesture. Its takes and Record (own gestures), what counts,
// Test (live match % against its line, and the 10 s misfire check), and what it does: the action
// (only what this situation can offer), how it ends, the stop sign, the confirm sign, its
// sensitivity and hold time. Own gestures can be renamed and deleted.
#pragma once

#include <QString>
#include <QWidget>

#include <map>

class QCheckBox;
class QLabel;
class QMenu;
class QLineEdit;
class QPushButton;
class QSlider;
class QVBoxLayout;

namespace racg::app {
class Controller;
}
namespace racg::gestures {
struct GestureDef;
}

namespace racg::ui {

class Meter;
class Picker;
class Switch;

class EditorPanel : public QWidget {
  Q_OBJECT
 public:
  explicit EditorPanel(app::Controller &c, QWidget *parent = nullptr);
  void open(const QString &gestureId);
  const QString &gestureId() const { return id_; }
  void fillActionMenu(QMenu &m);   // the Action menu (also for screenshots)

 Q_SIGNALS:
  void back();

 private:
  void build();
  void refreshTest();
  void actionMenu(QPoint at);
  void endsMenu(QPoint at);
  void holdMenu(QPoint at);
  void execMenu(QMenu &m, QPoint at);
  bool dirty_ = false;   // a menu pick changed the gesture
  gestures::GestureDef *g();

  app::Controller &c_;
  QString id_;
  QWidget *body_ = nullptr;
  QVBoxLayout *lay_ = nullptr;
  Meter *meter_ = nullptr;
  QLabel *matchVal_ = nullptr, *result_ = nullptr;
  QPushButton *testBtn_ = nullptr, *misBtn_ = nullptr;
  bool lastRecording_ = false;
  int lastSituation_ = -1;
  std::map<QString, QString> lastResult_;   // gesture id -> its last misfire result, this session
};

}  // namespace racg::ui
