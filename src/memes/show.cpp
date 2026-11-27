#include "memes/show.hpp"

#include "common/paths.hpp"

#include <QDir>

#include <cmath>

namespace racg::memes {

namespace {

// itsgiving: a square centred on the face, kFaceScale times the face's height.
constexpr float kFaceScale = 2.0f;
constexpr qint64 kFaceGraceMs = 1000;   // keeps its place this long after the face is lost

QRectF fitInto(QSizeF img, const QRectF &box) {
  const QSizeF s = img.scaled(box.size(), Qt::KeepAspectRatio);
  return {box.center().x() - s.width() / 2, box.center().y() - s.height() / 2, s.width(), s.height()};
}

}  // namespace

QString Show::start(const Meme &m, qint64 tMs) {
  QString err;
  std::shared_ptr<Media> media = Media::open(QDir(paths::mediaDir()).filePath(m.file), m.kind, &err);
  if (!media) return m.name() + " can't play: " + err;
  std::lock_guard l(mu_);
  playing_[m.id] = {m, std::move(media), tMs};
  if (m.place == Place::Head) headOwner_ = m.id;
  return {};
}

void Show::stop(const QString &memeId) {
  std::lock_guard l(mu_);
  playing_.erase(memeId);
  if (headOwner_ == memeId) {
    headOwner_.clear();
    // The slot goes to the newest other head meme still showing.
    qint64 newest = -1;
    for (const auto &[id, p] : playing_)
      if (p.meme.place == Place::Head && p.since > newest) {
        newest = p.since;
        headOwner_ = id;
      }
  }
}

void Show::stopAll() {
  std::lock_guard l(mu_);
  playing_.clear();
  headOwner_.clear();
}

bool Show::any() const {
  std::lock_guard l(mu_);
  return !playing_.empty();
}

void Show::refresh(const Library &lib) {
  std::lock_guard l(mu_);
  for (auto &[id, p] : playing_)
    if (const Meme *m = lib.find(id)) {
      p.meme.place = m->place;
      p.meme.spot = m->spot;
    }
}

void Show::draw(QPainter &p, const output::FrameInfo &info, const output::DetectionState &det, qint64 tMs) {
  std::lock_guard l(mu_);
  if (playing_.empty()) return;
  const QSizeF out = info.size;

  // The head box: the face's landmarks mapped into this frame, then smoothed (itsgiving's 0.7 per
  // frame at 30 fps, made per second).
  if (!headOwner_.isEmpty()) {
    if (det.obs && det.obs->face) {
      const output::Mapping m = info.fromObs();
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
      for (const auto &pt : det.obs->face->lm) {
        const QPointF q = m(pt.xy());
        x0 = std::min(x0, float(q.x()));
        y0 = std::min(y0, float(q.y()));
        x1 = std::max(x1, float(q.x()));
        y1 = std::max(y1, float(q.y()));
      }
      const float side = (y1 - y0) * kFaceScale;
      const QRectF target((x0 + x1) / 2 - side / 2, (y0 + y1) / 2 - side / 2, side, side);
      if (headAt_ < 0 || head_.isEmpty()) {
        head_ = target;
      } else {
        const double dt = std::clamp((tMs - headAt_) / 1000.0, 0.0, 0.5);
        const double keep = std::pow(0.7, dt * 30);
        head_ = QRectF(head_.x() * keep + target.x() * (1 - keep), head_.y() * keep + target.y() * (1 - keep),
                       head_.width() * keep + target.width() * (1 - keep), head_.height() * keep + target.height() * (1 - keep));
      }
      faceSeen_ = tMs;
    } else if (head_.isEmpty()) {
      // No face yet: the upper middle, about where a face would be.
      const double side = out.height() * 0.55;
      head_ = QRectF(out.width() / 2 - side / 2, out.height() * 0.08, side, side);
    }
    headAt_ = tMs;
  }

  for (auto &[id, pl] : playing_) {
    const QImage img = pl.media->frameAt((tMs - pl.since) / 1000.0);
    if (img.isNull()) continue;
    QRectF box;
    switch (pl.meme.place) {
      case Place::Head:
        if (id != headOwner_) continue;
        if (faceSeen_ >= 0 && tMs - faceSeen_ > kFaceGraceMs && !(det.obs && det.obs->face)) continue;   // face gone
        box = fitInto(img.size(), head_);
        break;
      case Place::Full:
        box = fitInto(img.size(), QRectF(QPointF(0, 0), out));
        break;
      case Place::Spot:
        box = fitInto(img.size(), QRectF(pl.meme.spot.x() * out.width(), pl.meme.spot.y() * out.height(),
                                         pl.meme.spot.width() * out.width(), pl.meme.spot.height() * out.height()));
        break;
    }
    p.drawImage(box, img);
  }
}

}  // namespace racg::memes
