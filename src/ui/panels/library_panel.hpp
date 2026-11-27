// The side panel's first page: the Controls, Reactions and Memes tabs. Controls is the firm list
// of jobs (gestures/controls.hpp), each with its one gesture and an on/off switch, plus the stop
// and confirm signs. Reactions lists Mine, itsgiving's reactions and Google's hand signs as
// collapsible groups (icon, name, "-> meme", on/off); a gesture a control uses says so, greyed. A
// click opens the editor. Memes lists the library's pictures, GIFs and videos.
#pragma once

#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QStackedWidget;
class QComboBox;

class QTimer;

namespace racg::app {
class Controller;
}

namespace racg::ui {

class LibraryPanel : public QWidget {
  Q_OBJECT
 public:
  explicit LibraryPanel(app::Controller &c, QWidget *parent = nullptr);
  void rebuild();
  bool memesTab() const;
  enum Tab { Controls, Reactions, Memes };
  void showTab(int t) { setTab(t); }
  void openGroups() { open_[0] = open_[1] = open_[2] = true; rebuild(); }

 Q_SIGNALS:
  void openGesture(const QString &id);
  void recordNew();
  void addMemes();
  void importItsgiving();
  // The meme whose fixed spot can be dragged on the preview now (empty for none).
  void spotMeme(const QString &memeId);

 private:
  void setTab(int t);
  void buildControls();
  QWidget *controlRow(const QString &id);
  QComboBox *triggerCombo(const QString &id);
  QWidget *signRow(bool confirm);
  QWidget *addObsControl();
  void buildGestures();
  void buildMemes();
  QWidget *gestureRow(const QString &id);
  QWidget *memeRow(const QString &id);

  app::Controller &c_;
  int tab_ = 0, lastSituation_ = -1;
  QTimer *rebuildLater_ = nullptr;
  bool open_[3] = {true, false, false};   // Mine, reactions, signs
  QPushButton *tabBtn_[3]{};
  QWidget *list_ = nullptr;
  QVBoxLayout *listLay_ = nullptr;
  QStackedWidget *foot_ = nullptr;
};

}  // namespace racg::ui
