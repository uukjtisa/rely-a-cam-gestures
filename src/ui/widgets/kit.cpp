#include "ui/widgets/kit.hpp"

#include "ui/theme.hpp"

#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidgetAction>

namespace racg::ui {

using namespace theme;

// ---- Switch --------------------------------------------------------------------------------------

Switch::Switch(const QString &text, QWidget *parent) : QAbstractButton(parent) {
  setCheckable(true);
  setText(text);
  setCursor(Qt::PointingHandCursor);
}

QSize Switch::sizeHint() const {
  const int tw = text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text()) + 6;
  return {30 + tw, 18};
}

void Switch::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF track(0, (height() - 16) / 2.0, 30, 16);
  p.setPen(Qt::NoPen);
  p.setBrush(isChecked() ? blue2 : grey3);
  p.drawRoundedRect(track, 8, 8);
  p.setBrush(theme::textColor);
  const double kx = isChecked() ? track.left() + 16 : track.left() + 2;
  p.drawEllipse(QRectF(kx, track.top() + 2, 12, 12));
  if (!QAbstractButton::text().isEmpty()) {
    p.setPen(isEnabled() ? theme::textColor : muted);
    p.drawText(QRect(36, 0, width() - 36, height()), Qt::AlignVCenter | Qt::AlignLeft, QAbstractButton::text());
  }
}

// ---- Chip ----------------------------------------------------------------------------------------

Chip::Chip(const QString &text, Kind kind, QWidget *parent) : QWidget(parent), text_(text), kind_(kind) {
  QFont f = font();
  f.setPixelSize(12);
  setFont(f);
}

void Chip::setText(const QString &t) {
  text_ = t;
  updateGeometry();
  update();
}

void Chip::setKind(Kind k) {
  kind_ = k;
  updateGeometry();
  update();
}

QSize Chip::sizeHint() const {
  return {fontMetrics().horizontalAdvance(text_) + 18 + (kind_ == Ok ? 12 : 0), 20};
}

void Chip::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(kind_ == Rec ? warnFill : grey5);
  p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
  int x = 9;
  if (kind_ == Ok) {
    p.setBrush(green3);
    p.drawEllipse(QRectF(x, height() / 2.0 - 3.5, 7, 7));
    x += 12;
  }
  p.setPen(kind_ == Rec ? theme::textColor : textLight);
  p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

void Chip::mouseReleaseEvent(QMouseEvent *e) {
  if (rect().contains(e->position().toPoint())) Q_EMIT clicked();
}

// ---- Meter ---------------------------------------------------------------------------------------

Meter::Meter(QWidget *parent) : QWidget(parent) { setMinimumHeight(12); }

void Meter::set(float pct, float line) {
  pct_ = pct;
  line_ = line;
  update();
}

void Meter::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF bar(0, (height() - 8) / 2.0, width(), 8);
  QPainterPath clip;
  clip.addRoundedRect(bar, 4, 4);
  p.setClipPath(clip);
  p.fillRect(bar, grey4);
  p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(pct_, 0.f, 100.f) / 100, bar.height()), green3);
  p.setClipping(false);
  const double x = bar.left() + bar.width() * line_ / 100;
  p.fillRect(QRectF(x - 1, bar.top() - 2, 2, bar.height() + 4), theme::textColor);
}

// ---- Picker --------------------------------------------------------------------------------------

Picker::Picker(QWidget *parent) : QWidget(parent) {
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(24);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Picker::setText(const QString &t, bool needs) {
  text_ = t;
  needs_ = needs;
  update();
}

void Picker::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(underMouse() ? grey1.lighter(115) : grey1);
  p.setBrush(underMouse() ? grey3 : grey4);
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
  p.setPen(needs_ ? need : theme::textColor);
  const QRect tr = rect().adjusted(8, 0, -22, 0);
  p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(text_, Qt::ElideRight, tr.width()));
  p.setPen(theme::textColor);
  p.drawText(rect().adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, "▾");
}

void Picker::mouseReleaseEvent(QMouseEvent *e) {
  if (rect().contains(e->position().toPoint())) Q_EMIT clicked(mapToGlobal(QPoint(0, height() + 2)));
}

// ---- GroupHeader ---------------------------------------------------------------------------------

GroupHeader::GroupHeader(const QString &title, int count, bool open, QWidget *parent)
    : QWidget(parent), title_(title), count_(count), open_(open) {
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(22);
}

void GroupHeader::paintEvent(QPaintEvent *) {
  QPainter p(this);
  QFont f = font();
  f.setPixelSize(11);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
  p.setFont(f);
  int x = 0;
  const QString arrow = open_ ? "▾ " : "▸ ";
  p.setPen(muted);
  p.drawText(QRect(x, 6, 20, height() - 6), Qt::AlignLeft | Qt::AlignVCenter, arrow);
  x += QFontMetrics(f).horizontalAdvance(arrow);
  f.setBold(true);
  p.setFont(f);
  p.setPen(textLight);
  const QString t = title_.toUpper();
  p.drawText(QRect(x, 6, width() - x, height() - 6), Qt::AlignLeft | Qt::AlignVCenter, t);
  x += QFontMetrics(f).horizontalAdvance(t);
  f.setBold(false);
  p.setFont(f);
  p.setPen(muted);
  p.drawText(QRect(x, 6, width() - x, height() - 6), Qt::AlignLeft | Qt::AlignVCenter, QString(" (%1)").arg(count_));
}

// ---- helpers -------------------------------------------------------------------------------------

QLabel *sectionTitle(const QString &text, QWidget *parent) {
  auto *l = new QLabel(text.toUpper(), parent);
  l->setProperty("h3", true);
  return l;
}

QLabel *note(const QString &text, QWidget *parent) {
  auto *l = new QLabel(text, parent);
  l->setProperty("muted", true);
  l->setWordWrap(true);
  return l;
}

void addMenuHeader(QMenu *m, const QString &text) {
  auto *a = new QWidgetAction(m);
  auto *l = new QLabel(text.toUpper());
  l->setObjectName("menuHdr");
  a->setDefaultWidget(l);
  a->setEnabled(false);
  m->addAction(a);
}

namespace {
class KeepOpen : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject *o, QEvent *e) override {
    auto *menu = qobject_cast<QMenu *>(o);
    if (!menu) return false;
    const bool click = e->type() == QEvent::MouseButtonRelease &&
                       static_cast<QMouseEvent *>(e)->button() == Qt::LeftButton;
    const bool key = e->type() == QEvent::KeyPress &&
                     (static_cast<QKeyEvent *>(e)->key() == Qt::Key_Return ||
                      static_cast<QKeyEvent *>(e)->key() == Qt::Key_Enter ||
                      static_cast<QKeyEvent *>(e)->key() == Qt::Key_Space);
    if (!click && !key) return false;
    QAction *a = menu->activeAction();
    if (!a || !a->isCheckable() || !a->isEnabled() || a->menu()) return false;
    if (click && !menu->actionGeometry(a).contains(static_cast<QMouseEvent *>(e)->position().toPoint())) return false;
    a->trigger();
    return true;
  }
};
}  // namespace

void keepMenusOpenOnToggle(QObject *app) { app->installEventFilter(new KeepOpen(app)); }

void addMenuNote(QMenu *m, const QString &text) {
  auto *a = new QWidgetAction(m);
  auto *l = new QLabel(text);
  l->setObjectName("menuNote");
  l->setWordWrap(true);
  l->setMaximumWidth(280);
  a->setDefaultWidget(l);
  a->setEnabled(false);
  m->addAction(a);
}

}  // namespace racg::ui
