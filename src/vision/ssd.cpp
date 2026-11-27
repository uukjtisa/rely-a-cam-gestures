#include "vision/ssd.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace racg::vision {

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
}  // namespace

SsdDecoder::SsdDecoder(SsdConfig c) : c_(std::move(c)) {
  // SsdAnchorsCalculator with fixed_anchor_size, aspect_ratios {1} and the default
  // interpolated_scale_aspect_ratio 1: two anchors per location per layer, layers of the same stride
  // merged. Only the centres matter, since every anchor is 1 x 1.
  const int n = int(c_.strides.size());
  int layer = 0;
  while (layer < n) {
    int perLocation = 0, last = layer;
    while (last < n && c_.strides[last] == c_.strides[layer]) {
      perLocation += 2;
      ++last;
    }
    const int fh = int(std::ceil(1.0f * c_.inputH / c_.strides[layer]));
    const int fw = int(std::ceil(1.0f * c_.inputW / c_.strides[layer]));
    for (int y = 0; y < fh; ++y)
      for (int x = 0; x < fw; ++x)
        for (int a = 0; a < perLocation; ++a) anchors_.push_back({(x + 0.5f) / fw, (y + 0.5f) / fh});
    layer = last;
  }
}

std::vector<Detection> SsdDecoder::decode(const float *boxes, const float *scores) const {
  // TensorsToDetections: reverse_output_order (x, y, w, h), scales = input size, sigmoid scores
  // clipped at +-100, keep >= min score.
  const float sx = float(c_.inputW), sy = float(c_.inputH);
  std::vector<Detection> all;
  for (int i = 0; i < c_.numBoxes && i < int(anchors_.size()); ++i) {
    const float s = sigmoid(std::clamp(scores[i], -100.f, 100.f));
    if (s < c_.minScore) continue;
    const float *b = boxes + size_t(i) * c_.numCoords;
    const Vec2 a = anchors_[i];
    const float xc = b[0] / sx + a.x, yc = b[1] / sy + a.y, w = b[2] / sx, h = b[3] / sy;
    Detection d;
    d.score = s;
    d.box = {xc - w / 2, yc - h / 2, w, h};
    for (int k = 0; k < c_.numKeypoints; ++k)
      d.keypoints.push_back({b[4 + 2 * k] / sx + a.x, b[4 + 2 * k + 1] / sy + a.y});
    all.push_back(std::move(d));
  }
  // Weighted NMS: the best box absorbs every box overlapping it by more than the threshold
  // (itself included), averaged by score; the output keeps the best box's score.
  std::stable_sort(all.begin(), all.end(), [](const auto &x, const auto &y) { return x.score > y.score; });
  std::vector<Detection> out;
  std::vector<Detection> remaining = std::move(all);
  while (!remaining.empty()) {
    const Detection &top = remaining[0];
    std::vector<Detection> keep;
    float total = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::vector<Vec2> kp(top.keypoints.size());
    for (const auto &r : remaining) {
      if (iou(r.box, top.box) > c_.nmsThreshold) {
        total += r.score;
        x0 += r.box.x * r.score;
        y0 += r.box.y * r.score;
        x1 += r.box.right() * r.score;
        y1 += r.box.bottom() * r.score;
        for (size_t k = 0; k < kp.size(); ++k) kp[k] += r.keypoints[k] * r.score;
      } else {
        keep.push_back(r);
      }
    }
    Detection w = top;
    if (total > 0) {
      w.box = {x0 / total, y0 / total, x1 / total - x0 / total, y1 / total - y0 / total};
      for (size_t k = 0; k < kp.size(); ++k) w.keypoints[k] = kp[k] / total;
    }
    out.push_back(std::move(w));
    if (keep.size() == remaining.size()) break;
    remaining = std::move(keep);
  }
  return out;
}

SsdConfig palmConfig() {
  SsdConfig c;
  c.inputW = c.inputH = 192;
  c.strides = {8, 16, 16, 16};
  c.numBoxes = 2016;
  c.numCoords = 18;
  c.numKeypoints = 7;
  return c;
}

SsdConfig faceConfig() {
  SsdConfig c;
  c.inputW = c.inputH = 128;
  c.strides = {8, 16, 16, 16};
  c.numBoxes = 896;
  c.numCoords = 16;
  c.numKeypoints = 6;
  c.nmsThreshold = 0.5f;   // face_detector_graph_options.proto min_suppression_threshold default
  return c;
}

SsdConfig poseConfig() {
  SsdConfig c;
  c.inputW = c.inputH = 224;
  c.strides = {8, 16, 32, 32, 32};
  c.numBoxes = 2254;
  c.numCoords = 12;
  c.numKeypoints = 4;
  c.nmsThreshold = 0.5f;   // pose_detector_graph_options.proto min_suppression_threshold default
  return c;
}

float rotationFrom(Vec2 a, Vec2 b, float target) {
  return normalizeRadians(target - std::atan2(-(b.y - a.y), b.x - a.x));
}

RotRect transformRect(RotRect r, float scale, float shiftX, float shiftY, bool squareLong) {
  if (r.rotation == 0.f) {
    r.cx += r.w * shiftX;
    r.cy += r.h * shiftY;
  } else {
    const float c = std::cos(r.rotation), s = std::sin(r.rotation);
    r.cx += r.w * shiftX * c - r.h * shiftY * s;
    r.cy += r.w * shiftX * s + r.h * shiftY * c;
  }
  if (squareLong) r.w = r.h = std::max(r.w, r.h);
  r.w *= scale;
  r.h *= scale;
  return r;
}

RotRect palmToHandRoi(const Detection &d) {
  // hand_detector_graph.cc sets rotation_vector_target_angle(90): the RADIANS field (the degrees one
  // is rotation_vector_target_angle_degrees), so MediaPipe aims the first crop of a hand at 90 rad
  // (116.6 deg), not 90 deg. Kept: it's what itsgiving's MediaPipe does, and the vision test matches
  // MediaPipe to the pixel with it. Tracking crops (handLandmarksToRoi) use a true 90 deg.
  const Vec2 c = d.box.center();
  RotRect r{c.x, c.y, d.box.w, d.box.h, rotationFrom(d.keypoints[0], d.keypoints[2], 90.f)};
  return transformRect(r, 2.6f, 0, -0.5f, true);
}

RotRect faceDetectionToRoi(const Detection &d) {
  const Vec2 c = d.box.center();
  RotRect r{c.x, c.y, d.box.w, d.box.h, rotationFrom(d.keypoints[0], d.keypoints[1], 0)};
  return transformRect(r, 1.5f, 0, 0, true);
}

static RotRect alignmentRect(Vec2 a, Vec2 b) {
  const float size = dist(a, b) * 2;
  return {a.x, a.y, size, size, rotationFrom(a, b, kPi / 2)};
}

RotRect poseDetectionToRoi(const Detection &d) {
  return transformRect(alignmentRect(d.keypoints[0], d.keypoints[1]), 1.25f, 0, 0, true);
}

RotRect poseLandmarksToRoi(const Landmark *aux) {
  return transformRect(alignmentRect(aux[0].xy(), aux[1].xy()), 1.25f, 0, 0, true);
}

RotRect handLandmarksToRoi(const Landmark *lm) {
  // HandLandmarksToRectCalculator as the tasks graph runs it: it's given all 21 landmarks, while its
  // joint constants (4, 6, 8) were written for the old 12-point subset, so MediaPipe really aims the
  // crop from the wrist toward the mean of the thumb tip, index PIP and index tip. Kept as is so
  // tracking behaves like MediaPipe's.
  const Vec2 w = lm[0].xy();
  const Vec2 mid = ((lm[4].xy() + lm[8].xy()) / 2 + lm[6].xy()) / 2;
  const float rotation = normalizeRadians(kPi / 2 - std::atan2(-(mid.y - w.y), mid.x - w.x));
  const float rev = normalizeRadians(-rotation);
  float ax0 = 1e9f, ay0 = 1e9f, ax1 = -1e9f, ay1 = -1e9f;
  for (int i = 0; i < 21; ++i) {
    ax0 = std::min(ax0, lm[i].x); ay0 = std::min(ay0, lm[i].y);
    ax1 = std::max(ax1, lm[i].x); ay1 = std::max(ay1, lm[i].y);
  }
  const Vec2 ac{(ax0 + ax1) / 2, (ay0 + ay1) / 2};
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  const float cr = std::cos(rev), sr = std::sin(rev);
  for (int i = 0; i < 21; ++i) {
    const float ox = lm[i].x - ac.x, oy = lm[i].y - ac.y;
    const float px = ox * cr - oy * sr, py = ox * sr + oy * cr;
    x0 = std::min(x0, px); y0 = std::min(y0, py); x1 = std::max(x1, px); y1 = std::max(y1, py);
  }
  const float pcx = (x0 + x1) / 2, pcy = (y0 + y1) / 2;
  const float c = std::cos(rotation), s = std::sin(rotation);
  RotRect r{pcx * c - pcy * s + ac.x, pcx * s + pcy * c + ac.y, x1 - x0, y1 - y0, rotation};
  return transformRect(r, 2.0f, 0, -0.1f, true);
}

RotRect faceLandmarksToRoi(const Landmark *lm) {
  // DetectionsToRects over the landmarks' bounding box (LandmarksToDetection), rotation from the
  // outer eye corners 33 -> 263, then x1.5 square.
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int i = 0; i < 478; ++i) {
    x0 = std::min(x0, lm[i].x); y0 = std::min(y0, lm[i].y);
    x1 = std::max(x1, lm[i].x); y1 = std::max(y1, lm[i].y);
  }
  RotRect r{(x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0, rotationFrom(lm[33].xy(), lm[263].xy(), 0)};
  return transformRect(r, 1.5f, 0, 0, true);
}

}  // namespace racg::vision
