// Every camera frame becomes the output here, on the capture thread at the camera's full rate:
// mirror -> framing crop (zoom, follow) -> camera off -> memes -> the output's overlays. The result
// goes to the outputs (virtual cameras) and is kept for the preview. Detection runs separately at
// 15 fps; its results are mapped onto each frame.
#pragma once

#include "output/overlay.hpp"

#include <QImage>
#include <QString>
#include <QRectF>

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace racg::output {

// Where an analysed-frame point lands in a composed frame, plus the frame's own size.
struct FrameInfo {
  QSize size;                 // the output frame
  QRectF crop{0, 0, 1, 1};    // the part of the (mirrored) camera picture shown, as fractions
  float obsToFull = 1;        // analysed-frame pixels -> camera pixels
  Mapping fromObs() const;    // analysed-frame pixels -> output pixels
};

// Something that takes the composed output (a virtual camera).
class Sink {
 public:
  virtual ~Sink() = default;
  virtual void send(const QImage &frame, qint64 timeMs) = 0;
  virtual QString error() const { return {}; }   // why it isn't reaching anyone, for the outputs chip
};

class Pipeline {
 public:
  // Capture thread.
  void process(const QImage &full, qint64 timeMs);
  // Attached to Rely-A-Cam: OBS draws the phone's own picture, so this makes only what goes on top
  // of it (memes and the output's overlays) on a transparent layer of `size`, for the framing crop
  // at that moment. `any` says whether anything was drawn. UI thread.
  QImage layer(QSize size, qint64 timeMs, QRectF *crop, bool *any);
  // A webcam picture is mirrored like a selfie; a phone's picture arrives already the right way.
  void setMirror(bool m);

  // The last composed frame (with the output's overlays) and its geometry. Any thread.
  QImage latest(FrameInfo *info = nullptr) const;
  // The last composed frame without overlays (snapshots). Any thread.
  QImage clean() const;

  // UI thread.
  void setDetection(DetectionState s);
  void setOutputOverlays(bool grid, bool detection, bool face);
  void setCameraOff(bool off, const QImage &picture);
  bool cameraOff() const;
  // The framing crop for a time, as fractions of the mirrored picture (M2's Framer).
  void setCropSource(std::function<QRectF(qint64)> f);
  // Draws the memes onto a composed frame (M2's meme player).
  void setMemeLayer(std::function<void(QPainter &, const FrameInfo &, const DetectionState &, qint64)> f);
  void addSink(std::shared_ptr<Sink> s);
  void removeSink(const Sink *s);

  std::function<void()> onFrame;   // capture thread, after each frame

 private:
  mutable std::mutex mu_;
  DetectionState det_;
  bool gridOut_ = false, detOut_ = false, faceOut_ = false, off_ = false, mirror_ = true;
  QImage offPicture_, latest_, clean_;
  FrameInfo info_;
  std::function<QRectF(qint64)> crop_;
  std::function<void(QPainter &, const FrameInfo &, const DetectionState &, qint64)> memes_;
  std::vector<std::shared_ptr<Sink>> sinks_;
};

}  // namespace racg::output
