#include "output/pipeline.hpp"

#include <QPainter>

namespace racg::output {

Mapping FrameInfo::fromObs() const {
  // The output keeps the camera's size W x H. An analysed point times obsToFull is a (mirrored)
  // camera pixel; as a fraction of W it's shifted by the crop's corner and stretched by 1 / crop:
  // out = (obs * obsToFull / W - crop.left) / crop.width * W.
  Mapping m;
  m.sx = float(obsToFull / crop.width());
  m.sy = float(obsToFull / crop.height());
  m.ox = float(-crop.left() * size.width() / crop.width());
  m.oy = float(-crop.top() * size.height() / crop.height());
  return m;
}

void Pipeline::process(const QImage &full, qint64 t) {
  DetectionState det;
  bool gridOut, detOut, off, mirror;
  QImage offPic;
  std::function<QRectF(qint64)> cropFn;
  std::function<void(QPainter &, const FrameInfo &, const DetectionState &, qint64)> memes;
  std::vector<std::shared_ptr<Sink>> sinks;
  {
    std::lock_guard l(mu_);
    det = det_;
    gridOut = gridOut_;
    detOut = detOut_;
    off = off_;
    mirror = mirror_;
    offPic = offPicture_;
    cropFn = crop_;
    memes = memes_;
    sinks = sinks_;
  }
  FrameInfo info;
  info.size = full.size();
  if (det.obs && det.obs->width > 0) info.obsToFull = float(full.width()) / det.obs->width;
  if (cropFn) info.crop = cropFn(t).intersected(QRectF(0, 0, 1, 1));
  if (info.crop.width() <= 0.01 || info.crop.height() <= 0.01) info.crop = QRectF(0, 0, 1, 1);

  QImage out;
  if (off) {
    out = QImage(full.size(), QImage::Format_RGB32);
    out.fill(Qt::black);
    if (!offPic.isNull()) {
      QPainter p(&out);
      p.setRenderHint(QPainter::SmoothPixmapTransform);
      const QImage s = offPic.scaled(out.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      p.drawImage((out.width() - s.width()) / 2, (out.height() - s.height()) / 2, s);
    }
  } else {
    // Mirror like the preview (itsgiving's default), then the framing crop, scaled back to full size.
    out = mirror ? full.mirrored(true, false) : full;
    if (info.crop != QRectF(0, 0, 1, 1)) {
      const QRect src(int(info.crop.left() * full.width()), int(info.crop.top() * full.height()),
                      std::max(2, int(info.crop.width() * full.width())), std::max(2, int(info.crop.height() * full.height())));
      out = out.copy(src).scaled(full.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (out.format() != QImage::Format_RGB32) out = out.convertToFormat(QImage::Format_RGB32);
  }
  if (memes && !off) {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    memes(p, info, det, t);
  }
  QImage cleanFrame = out;
  if (gridOut || (detOut && !off)) {
    out.detach();
    QPainter p(&out);
    if (gridOut) drawGrid(p, QRectF(out.rect()));
    if (detOut && !off) drawDetection(p, det, info.fromObs(), out.height() / 360.f);
  }
  for (const auto &s : sinks) s->send(out, t);
  {
    std::lock_guard l(mu_);
    latest_ = out;
    clean_ = cleanFrame;
    info_ = info;
  }
  if (onFrame) onFrame();
}

QImage Pipeline::layer(QSize size, qint64 t, QRectF *crop, bool *any) {
  DetectionState det;
  bool gridOut, detOut, off;
  QImage offPic;
  std::function<QRectF(qint64)> cropFn;
  std::function<void(QPainter &, const FrameInfo &, const DetectionState &, qint64)> memes;
  {
    std::lock_guard l(mu_);
    det = det_;
    gridOut = gridOut_;
    detOut = detOut_;
    off = off_;
    offPic = offPicture_;
    cropFn = crop_;
    memes = memes_;
  }
  FrameInfo info;
  info.size = size;
  if (det.obs && det.obs->width > 0) info.obsToFull = float(size.width()) / det.obs->width;
  if (cropFn) info.crop = cropFn(t).intersected(QRectF(0, 0, 1, 1));
  if (info.crop.width() <= 0.01 || info.crop.height() <= 0.01) info.crop = QRectF(0, 0, 1, 1);
  if (crop) *crop = info.crop;
  QImage out(size, QImage::Format_ARGB32_Premultiplied);
  // Camera off covers the phone's picture completely: the camera keeps watching for the gesture
  // that turns it back on, but nobody sees it.
  out.fill(off ? QColor(Qt::black) : QColor(Qt::transparent));
  bool drew = off;
  {
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    if (off && !offPic.isNull()) {
      const QImage s = offPic.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      p.drawImage((size.width() - s.width()) / 2, (size.height() - s.height()) / 2, s);
    }
    if (memes && !off) {
      memes(p, info, det, t);
      drew = true;   // the show draws nothing when nothing plays; the caller asks it separately
    }
    if (gridOut) {
      drawGrid(p, QRectF(out.rect()));
      drew = true;
    }
    if (detOut && !off && det.obs) {
      drawDetection(p, det, info.fromObs(), out.height() / 360.f);
      drew = true;
    }
  }
  if (any) *any = drew;
  return out;
}

void Pipeline::setMirror(bool m) {
  std::lock_guard l(mu_);
  mirror_ = m;
}

QImage Pipeline::latest(FrameInfo *info) const {
  std::lock_guard l(mu_);
  if (info) *info = info_;
  return latest_;
}

QImage Pipeline::clean() const {
  std::lock_guard l(mu_);
  return clean_;
}

void Pipeline::setDetection(DetectionState s) {
  std::lock_guard l(mu_);
  det_ = std::move(s);
}

void Pipeline::setOutputOverlays(bool grid, bool detection) {
  std::lock_guard l(mu_);
  gridOut_ = grid;
  detOut_ = detection;
}

void Pipeline::setCameraOff(bool off, const QImage &picture) {
  std::lock_guard l(mu_);
  off_ = off;
  offPicture_ = picture;
}

bool Pipeline::cameraOff() const {
  std::lock_guard l(mu_);
  return off_;
}

void Pipeline::setCropSource(std::function<QRectF(qint64)> f) {
  std::lock_guard l(mu_);
  crop_ = std::move(f);
}

void Pipeline::setMemeLayer(std::function<void(QPainter &, const FrameInfo &, const DetectionState &, qint64)> f) {
  std::lock_guard l(mu_);
  memes_ = std::move(f);
}

void Pipeline::addSink(std::shared_ptr<Sink> s) {
  std::lock_guard l(mu_);
  sinks_.push_back(std::move(s));
}

void Pipeline::removeSink(const Sink *s) {
  std::lock_guard l(mu_);
  std::erase_if(sinks_, [s](const auto &p) { return p.get() == s; });
}

}  // namespace racg::output
