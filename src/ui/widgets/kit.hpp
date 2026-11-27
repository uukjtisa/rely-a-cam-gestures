// The window's shared components (docs/DESIGN.md "Kit"). A new kind of control goes here, and into
// the standard, before any panel uses it.
#pragma once

#include <QAbstractButton>
#include <QLabel>
#include <QWidget>

class QMenu;

namespace racg::ui {

// The on/off switch: a 30 x 16 track with a 12 px knob, blue when on, with an optional label.
class Switch : public QAbstractButton {
  Q_OBJECT
 public:
  explicit Switch(const QString &text = {}, QWidget *parent = nullptr);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent *) override;
};

// A rounded status chip; Ok adds a green dot, Rec is the red recording chip.
class Chip : public QWidget {
  Q_OBJECT
 public:
  enum Kind { Plain, Ok, Rec };
  explicit Chip(const QString &text = {}, Kind kind = Plain, QWidget *parent = nullptr);
  void setText(const QString &t);
  void setKind(Kind k);
  QSize sizeHint() const override;

 Q_SIGNALS:
  void clicked();

 protected:
  void paintEvent(QPaintEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;

 private:
  QString text_;
  Kind kind_;
};

// The match meter: an 8 px bar filled to the match % with a white line at the gesture's line.
class Meter : public QWidget {
  Q_OBJECT
 public:
  explicit Meter(QWidget *parent = nullptr);
  void set(float pct, float line);
  QSize sizeHint() const override { return {120, 12}; }

 protected:
  void paintEvent(QPaintEvent *) override;

 private:
  float pct_ = 0, line_ = 60;
};

// A field that opens a menu: the current choice and a ▾ (the editor's Action and Ends).
class Picker : public QWidget {
  Q_OBJECT
 public:
  explicit Picker(QWidget *parent = nullptr);
  void setText(const QString &t, bool needs = false);
  QSize sizeHint() const override { return {200, 24}; }

 Q_SIGNALS:
  void clicked(QPoint globalBelow);

 protected:
  void paintEvent(QPaintEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void enterEvent(QEnterEvent *) override { update(); }
  void leaveEvent(QEvent *) override { update(); }

 private:
  QString text_;
  bool needs_ = false;
};

// A section's collapsible header in the library: "▾ MINE (4)".
class GroupHeader : public QWidget {
  Q_OBJECT
 public:
  GroupHeader(const QString &title, int count, bool open, QWidget *parent = nullptr);
  QSize sizeHint() const override { return {200, 22}; }

 Q_SIGNALS:
  void toggled();

 protected:
  void paintEvent(QPaintEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override { Q_EMIT toggled(); }

 private:
  QString title_;
  int count_;
  bool open_;
};

// A small uppercase section title (the editor's RECORDING, TEST, DOES).
QLabel *sectionTitle(const QString &text, QWidget *parent = nullptr);
// A muted 12 px line of text.
QLabel *note(const QString &text, QWidget *parent = nullptr);
// Menu pieces the stylesheet can't make: a small header, and a wrapped note.
void addMenuHeader(QMenu *m, const QString &text);
void addMenuNote(QMenu *m, const QString &text);

}  // namespace racg::ui
