#include "ui/widgets/preview.hpp"

#include "app/controller.hpp"
#include "ui/theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

namespace racg::ui {

using namespace theme;

Preview::Preview(app::Controller &c, QWidget *parent) : QWidget(parent), c_(c) {
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  setMinimumSize(320, 180);
  connect(&c_, &app::Controller::frame, this, qOverload<>(&QWidget::update));
  connect(&c_, &app::Controller::observed, this, qOverload<>(&QWidget::update));
  connect(&c_, &app::Controller::toastShown, this, &Preview::showToast);
}

void Preview::showToast(const QString &text) {
  toast_ = text;
  toastAt_.start();
  update();
  QTimer::singleShot(toastMs + 50, this, qOverload<>(&QWidget::update));
}

void Preview::setSpotMeme(const QString &memeId) {
  spotMeme_ = memeId;
  setCursor(memeId.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
  update();
}

QRectF Preview::pictureRect(QSize frame) const {
  if (frame.isEmpty()) return rect();
  QSizeF s = frame.scaled(size(), Qt::KeepAspectRatio);
  return QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

void Preview::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  QPainterPath clip;
  clip.addRoundedRect(QRectF(rect()), 3, 3);
  p.setClipPath(clip);
  p.fillRect(rect(), Qt::black);

  output::FrameInfo info;
  const QImage img = c_.pipeline().latest(&info);
  const QRectF pic = pictureRect(img.size());
  const Settings &s = c_.settings();
  if (img.isNull()) {
    p.setPen(muted);
    const QString msg = !c_.cameraError().isEmpty() ? c_.cameraError() : QString("Starting the camera...");
    p.drawText(rect().adjusted(20, 0, -20, 0), Qt::AlignCenter | Qt::TextWordWrap, msg);
  } else {
    p.drawImage(pic, img);
    // The preview's own overlays, unless the output already carries them.
    if (s.grid.preview && !s.grid.output) output::drawGrid(p, pic);
    const bool hands = s.detection.preview && !s.detection.output;
    const bool face = s.faceMap.preview && !s.faceMap.output;
    if ((hands || face) && !c_.pipeline().cameraOff()) {
      // The same state the controller hands the pipeline: gestures, the pinch moves, the face map.
      const output::DetectionState &ds = c_.detection();
      output::Mapping m = info.fromObs();
      const float k = float(pic.width() / std::max(1, img.width()));
      m.sx *= k;
      m.sy *= k;
      m.ox = m.ox * k + float(pic.left());
      m.oy = m.oy * k + float(pic.top());
      output::drawDetection(p, ds, m, float(pic.height() / 360.0), hands, face);
    }
  }

  // The meme whose fixed spot is being placed: its box, to drag.
  if (!spotMeme_.isEmpty())
    if (const memes::Meme *mm = c_.memes().find(spotMeme_)) {
      const QRectF r(pic.left() + mm->spot.x() * pic.width(), pic.top() + mm->spot.y() * pic.height(),
                     mm->spot.width() * pic.width(), mm->spot.height() * pic.height());
      p.setPen(QPen(blue1, 2, Qt::DashLine));
      p.setBrush(QColor(blue3.red(), blue3.green(), blue3.blue(), 60));
      p.drawRoundedRect(r, 6, 6);
      p.setPen(onImage);
      p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, mm->name() + "\ndrag to move, scroll to resize");
    }

  // Recording: 3-2-1, then "Hold it..." with a bar.
  const auto &rec = c_.recorder();
  const double now = c_.now();
  if (rec.phase() == gestures::Recorder::Phase::Countdown) {
    QFont f = font();
    f.setPixelSize(int(96 * height() / 428.0));
    f.setWeight(QFont::ExtraBold);
    p.setFont(f);
    p.setPen(scrim);
    p.drawText(rect().translated(0, 4), Qt::AlignCenter, QString::number(rec.countdown(now)));
    p.setPen(onImage);
    p.drawText(rect(), Qt::AlignCenter, QString::number(rec.countdown(now)));
    QTimer::singleShot(50, this, qOverload<>(&QWidget::update));
  } else if (rec.phase() == gestures::Recorder::Phase::Hold) {
    const QRectF bar(width() * 0.3, height() - 24, width() * 0.4, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(scrim);
    p.drawRoundedRect(bar, 4, 4);
    p.setBrush(blue2);
    p.drawRoundedRect(QRectF(bar.left(), bar.top(), bar.width() * rec.holdProgress(now), bar.height()), 4, 4);
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(onImage);
    p.drawText(QRectF(0, height() - 54, width(), 24), Qt::AlignCenter,
               QString("Hold it... %1 s").arg(rec.holdSeconds(now), 0, 'f', 1));
    QTimer::singleShot(50, this, qOverload<>(&QWidget::update));
  }

  // Calibration: a band on top with the time, the frames, and "no face" when it's lost.
  const auto &cal = c_.calibrator();
  if (cal.running()) {
    const QRectF band(0, 0, width(), 64);
    p.fillRect(band, scrim);
    QFont f = font();
    f.setPixelSize(15);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(onImage);
    p.drawText(band.adjusted(14, 8, -14, -30), Qt::AlignLeft | Qt::AlignVCenter, "Calibrating: hold a bored face");
    f.setPixelSize(12);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(cal.faceNow() ? onImage : yellow2);
    p.drawText(band.adjusted(14, 30, -14, -10), Qt::AlignLeft | Qt::AlignVCenter,
               QString("%1 s   %2 frames%3").arg(cal.secondsLeft(now), 0, 'f', 1).arg(cal.samples()).arg(cal.faceNow() ? "" : "   no face"));
    p.fillRect(QRectF(0, band.bottom() - 4, width() * cal.progress(now), 4), green3);
    QTimer::singleShot(50, this, qOverload<>(&QWidget::update));
  }

  // A toast: a white pill at the top for a couple of seconds.
  if (!toast_.isEmpty() && toastAt_.isValid() && toastAt_.elapsed() < toastMs) {
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const QString t = fm.elidedText(toast_, Qt::ElideRight, width() - 60);
    const double w = fm.horizontalAdvance(t) + 28, h = 26;
    const QRectF r((width() - w) / 2, cal.running() ? 72 : 12, w, h);
    p.setPen(Qt::NoPen);
    p.setBrush(toastFill);
    p.drawRoundedRect(r, h / 2, h / 2);
    p.setPen(toastText);
    p.drawText(r, Qt::AlignCenter, t);
  }
}

void Preview::mousePressEvent(QMouseEvent *e) {
  if (spotMeme_.isEmpty()) return;
  memes::Meme *m = c_.memes().find(spotMeme_);
  if (!m) return;
  const QRectF pic = pictureRect(c_.pipeline().latest().size());
  const QPointF f((e->position().x() - pic.left()) / pic.width(), (e->position().y() - pic.top()) / pic.height());
  dragging_ = true;
  // Grab inside the box keeps the grip; outside it moves the box's centre there.
  dragOffset_ = m->spot.contains(f) ? f - m->spot.topLeft() : QPointF(m->spot.width() / 2, m->spot.height() / 2);
  setCursor(Qt::ClosedHandCursor);
  mouseMoveEvent(e);
}

void Preview::mouseMoveEvent(QMouseEvent *e) {
  if (!dragging_) return;
  memes::Meme *m = c_.memes().find(spotMeme_);
  if (!m) return;
  const QRectF pic = pictureRect(c_.pipeline().latest().size());
  const QPointF f((e->position().x() - pic.left()) / pic.width(), (e->position().y() - pic.top()) / pic.height());
  QPointF tl = f - dragOffset_;
  tl.setX(std::clamp(tl.x(), 0.0, 1.0 - m->spot.width()));
  tl.setY(std::clamp(tl.y(), 0.0, 1.0 - m->spot.height()));
  m->spot.moveTopLeft(tl);
  update();
}

void Preview::mouseReleaseEvent(QMouseEvent *) {
  if (!dragging_) return;
  dragging_ = false;
  setCursor(Qt::OpenHandCursor);
  c_.memesEdited();
  Q_EMIT spotMoved();
}

void Preview::wheelEvent(QWheelEvent *e) {
  memes::Meme *m = spotMeme_.isEmpty() ? nullptr : c_.memes().find(spotMeme_);
  if (!m) return;
  const double k = e->angleDelta().y() > 0 ? 1.08 : 1 / 1.08;
  const QPointF c = m->spot.center();
  const double w = std::clamp(m->spot.width() * k, 0.06, 1.0), h = std::clamp(m->spot.height() * k, 0.06, 1.0);
  QRectF r(c.x() - w / 2, c.y() - h / 2, w, h);
  r.moveLeft(std::clamp(r.left(), 0.0, 1.0 - w));
  r.moveTop(std::clamp(r.top(), 0.0, 1.0 - h));
  m->spot = r;
  update();
  c_.memesEdited();
}

}  // namespace racg::ui
