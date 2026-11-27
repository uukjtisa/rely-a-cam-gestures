#include "vision/detector.hpp"

#include "gestures/itsgiving.hpp"

#include <QElapsedTimer>

#include <chrono>

namespace racg::vision {

Detector::Detector() = default;
Detector::~Detector() { stop(); }

void Detector::start(const QString &modelsDir, bool preferGpu) {
  stop();
  stop_ = false;
  thread_ = std::thread(&Detector::run, this, modelsDir, preferGpu);
}

void Detector::stop() {
  {
    std::lock_guard l(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void Detector::submit(const QImage &full, qint64 timeMs) {
  {
    std::lock_guard l(mu_);
    pending_ = full;
    pendingTime_ = timeMs;
    hasPending_ = true;
  }
  cv_.notify_one();
}

void Detector::run(QString dir, bool gpu) {
  std::unique_ptr<Models> models;
  try {
    models = std::make_unique<Models>(std::filesystem::path(dir.toStdWString()), gpu);
  } catch (const std::exception &e) {
    if (onReady) onReady(false, {}, QString::fromLocal8Bit(e.what()));
    return;
  }
  if (onReady) onReady(true, deviceName(models->device()), {});
  HandLandmarker hands(*models);
  FaceLandmarker faces(*models);
  PoseLandmarker poses(*models);
  int64_t frameId = 0;
  auto lastStart = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  while (true) {
    QImage full;
    qint64 time = 0;
    {
      // At most fps a second: wait for a frame, then for the slot, then take whatever is newest.
      std::unique_lock l(mu_);
      cv_.wait(l, [&] { return stop_ || hasPending_; });
      if (stop_) return;
      const auto next = lastStart + std::chrono::microseconds(int64_t(1e6 / std::max(1.f, fps_.load())));
      if (std::chrono::steady_clock::now() < next) {
        cv_.wait_until(l, next, [&] { return stop_.load(); });
        if (stop_) return;
      }
      full = std::move(pending_);
      time = pendingTime_;
      hasPending_ = false;
      lastStart = std::chrono::steady_clock::now();
    }
    if (reset_.exchange(false)) {
      hands.reset();
      faces.reset();
      poses.reset();
    }
    QElapsedTimer t;
    t.start();
    auto obs = std::make_shared<Observation>();
    obs->frameId = ++frameId;
    obs->timeMs = time;
    obs->full = full;
    obs->mirrored = mirror_;
    QImage small = full.scaled(kWidth, kHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (obs->mirrored) small = small.mirrored(true, false);
    small = small.convertToFormat(QImage::Format_RGB32);
    obs->frame = small;
    obs->width = small.width();
    obs->height = small.height();
    obs->fullScale = float(full.width()) / std::max(1, small.width());
    obs->hands = hands.process(small);
    obs->face = faces.process(small);
    obs->body = poses.process(small);
    if (obs->face) {
      std::array<Vec2, 20> lips;
      for (size_t i = 0; i < lips.size(); ++i) lips[i] = obs->toFull(obs->face->lm[gestures::kInnerLips[i]].xy());
      obs->mouthPink = gestures::mouthPink(full, lips);
    }
    obs->ms = float(t.nsecsElapsed() / 1e6);
    if (onObservation) onObservation(obs);
  }
}

}  // namespace racg::vision
