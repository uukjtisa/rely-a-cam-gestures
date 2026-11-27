// The side panel's first page: the Gestures and Memes tabs. Gestures lists Mine, itsgiving's
// reactions and Google's hand signs as collapsible groups of rows (icon, name, "-> action" or
// "needs OBS", on/off switch); a click opens the editor. Memes lists the library's pictures, GIFs
// and videos with their placement and sound.
#pragma once

#include <QWidget>

class QVBoxLayout;
class QPushButton;
class QStackedWidget;

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
  void showTab(int t) { setTab(t); }   // 0 Gestures, 1 Memes
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
  void buildGestures();
  void buildMemes();
  QWidget *gestureRow(const QString &id);
  QWidget *memeRow(const QString &id);

  app::Controller &c_;
  int tab_ = 0, lastSituation_ = -1;
  bool open_[3] = {true, false, false};   // Mine, reactions, signs
  QPushButton *tabBtn_[2]{};
  QWidget *list_ = nullptr;
  QVBoxLayout *listLay_ = nullptr;
  QStackedWidget *foot_ = nullptr;
};

}  // namespace racg::ui
