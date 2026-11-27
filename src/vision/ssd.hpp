// BlazeFace-style SSD detectors (palm, face, pose): anchors, box decoding and weighted NMS, ported
// from MediaPipe's SsdAnchorsCalculator, TensorsToDetectionsCalculator and
// NonMaxSuppressionCalculator (WEIGHTED, IoU) with the options each task graph sets.
// Also the rectangle rules that turn a detection or landmarks into the next model's crop.
#pragma once

#include "common/geometry.hpp"

#include <vector>

namespace racg::vision {

struct SsdConfig {
  int inputW = 0, inputH = 0;
  float minScale = 0.1484375f, maxScale = 0.75f;
  std::vector<int> strides;
  int numBoxes = 0, numCoords = 0, numKeypoints = 0;
  float minScore = 0.5f;          // MediaPipe's min_detection_confidence default
  float nmsThreshold = 0.3f;      // the palm's (hand_detector_graph.cc); face and pose use 0.5
};

// Box and keypoints in frame pixels once projected (decode() returns them in tensor units [0, 1]).
struct Detection {
  float score = 0;
  Box box;
  std::vector<Vec2> keypoints;
};

class SsdDecoder {
 public:
  explicit SsdDecoder(SsdConfig c);
  // boxes: [numBoxes, numCoords]; scores: [numBoxes] raw logits. Sorted by score, NMS applied.
  std::vector<Detection> decode(const float *boxes, const float *scores) const;
  const SsdConfig &config() const { return c_; }
  size_t anchorCount() const { return anchors_.size(); }

 private:
  SsdConfig c_;
  std::vector<Vec2> anchors_;   // centres; every anchor is fixed-size (1 x 1)
};

// The three detectors' settings, straight from MediaPipe's graphs.
SsdConfig palmConfig();   // hand_detector_graph.cc, 192 x 192
SsdConfig faceConfig();   // face_detector_graph.cc, short range, 128 x 128
SsdConfig poseConfig();   // pose_detector_graph.cc, 224 x 224

// MediaPipe's rotation rule: the angle that turns the (a -> b) vector to `target`.
float rotationFrom(Vec2 a, Vec2 b, float target);

// RectTransformationCalculator, in pixels.
RotRect transformRect(RotRect r, float scale, float shiftX, float shiftY, bool squareLong);

// Next-frame / detection crops for each model.
RotRect palmToHandRoi(const Detection &d);           // kp 0 -> 2, 90 deg; x2.6, shift -0.5
RotRect faceDetectionToRoi(const Detection &d);      // kp 0 -> 1, 0 deg; x1.5
RotRect poseDetectionToRoi(const Detection &d);      // alignment kp 0 -> 1, 90 deg; x1.25
RotRect handLandmarksToRoi(const Landmark *lm);      // 21 points; x2.0, shift -0.1
RotRect faceLandmarksToRoi(const Landmark *lm);      // 478 points, eyes 33 -> 263; x1.5
RotRect poseLandmarksToRoi(const Landmark *aux);     // aux points 33, 34 (two landmarks); x1.25

}  // namespace racg::vision
