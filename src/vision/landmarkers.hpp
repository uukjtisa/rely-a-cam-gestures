// The three MediaPipe landmarkers, rebuilt on ONNX Runtime: find (detector) -> crop -> landmarks
// -> track. Each one keeps last frame's crops and only runs its detector when it's tracking fewer
// than it may find (MediaPipe's VIDEO mode). reset() forgets the tracks (IMAGE mode per call).
#pragma once

#include "vision/observation.hpp"
#include "vision/onnx.hpp"
#include "vision/ssd.hpp"

#include <QImage>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace racg::vision {

struct ModelSet;   // all nine models, loaded once

class Models {
 public:
  // dir holds the .onnx files from scripts/fetch_models.py. Throws std::runtime_error.
  Models(const std::filesystem::path &dir, bool preferGpu);
  ~Models();
  Device device() const;      // DirectML if every model got the GPU, else CPU
  ModelSet &set() { return *m_; }

 private:
  std::unique_ptr<ModelSet> m_;
};

class HandLandmarker {
 public:
  explicit HandLandmarker(Models &m, int maxHands = 2);
  std::vector<Hand> process(const QImage &frame);
  void reset() { tracks_.clear(); }

 private:
  std::optional<Hand> landmarks(const QImage &frame, const RotRect &roi);
  void classify(Hand &h, int w, int h2);
  ModelSet &m_;
  int max_;
  SsdDecoder palm_;
  std::vector<RotRect> tracks_;
};

class FaceLandmarker {
 public:
  explicit FaceLandmarker(Models &m);
  std::optional<Face> process(const QImage &frame);
  void reset() { track_.reset(); }

 private:
  std::optional<Face> landmarks(const QImage &frame, const RotRect &roi);
  ModelSet &m_;
  SsdDecoder det_;
  std::optional<RotRect> track_;
};

class PoseLandmarker {
 public:
  explicit PoseLandmarker(Models &m);
  std::optional<Body> process(const QImage &frame);
  void reset() { track_.reset(); }

 private:
  std::optional<Body> landmarks(const QImage &frame, const RotRect &roi, RotRect *next);
  ModelSet &m_;
  SsdDecoder det_;
  std::optional<RotRect> track_;
};

}  // namespace racg::vision
