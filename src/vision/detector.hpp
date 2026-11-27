// The detector thread: takes the newest camera frame (older ones are dropped), at most 15 times a
// second scales it to fit 640 x 360 and mirrors it like the preview, and runs hands, face and body
// on it. Memes and the output run at the camera's full rate; only seeing runs at 15 fps.
#pragma once

#include "vision/landmarkers.hpp"
#include "vision/observation.hpp"

#include <QImage>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace racg::vision {

class Detector {
 public:
  static constexpr int kWidth = 640, kHeight = 360;

  Detector();
  ~Detector();
  // Loads the models (a second or two) on the detector thread; onReady reports how it went.
  void start(const QString &modelsDir, bool preferGpu);
  void stop();

  // Any thread: the newest frame (never mirrored) and when it was taken.
  void submit(const QImage &full, qint64 timeMs);
  void setMirror(bool m) { mirror_ = m; }
  void setFps(float fps) { fps_ = fps; }
  void resetTracking() { reset_ = true; }

  // On the detector thread.
  std::function<void(std::shared_ptr<const Observation>)> onObservation;
  std::function<void(bool ok, const QString &device, const QString &error)> onReady;

 private:
  void run(QString dir, bool gpu);
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  QImage pending_;
  qint64 pendingTime_ = 0;
  bool hasPending_ = false;
  std::atomic<bool> stop_{false}, mirror_{true}, reset_{false};
  std::atomic<float> fps_{15.f};
};

}  // namespace racg::vision
