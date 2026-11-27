#include "vision/landmarkers.hpp"

#include "vision/crop.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace racg::vision {

struct ModelSet {
  std::unique_ptr<Model> palm, hand, embedder, classifier, faceDet, face, blend, poseDet, pose;
};

namespace {
constexpr float kPresence = 0.5f;   // MediaPipe's min_*_presence_confidence defaults
constexpr float kAssociation = 0.5f;

// MediaPipe's face_blendshapes_graph.cc kLandmarksSubsetIdxs.
constexpr int kBlendSubset[146] = {
    0,   1,   4,   5,   6,   7,   8,   10,  13,  14,  17,  21,  33,  37,  39,  40,  46,  52,  53,  54,  55,
    58,  61,  63,  65,  66,  67,  70,  78,  80,  81,  82,  84,  87,  88,  91,  93,  95,  103, 105, 107, 109,
    127, 132, 133, 136, 144, 145, 146, 148, 149, 150, 152, 153, 154, 155, 157, 158, 159, 160, 161, 162, 163,
    168, 172, 173, 176, 178, 181, 185, 191, 195, 197, 234, 246, 249, 251, 263, 267, 269, 270, 276, 282, 283,
    284, 285, 288, 291, 293, 295, 296, 297, 300, 308, 310, 311, 312, 314, 317, 318, 321, 323, 324, 332, 334,
    336, 338, 356, 361, 362, 365, 373, 374, 375, 377, 378, 379, 380, 381, 382, 384, 385, 386, 387, 388, 389,
    390, 397, 398, 400, 402, 405, 409, 415, 454, 466, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477};

// The rotated crop of `roi` mapped back: tensor (x, y) in [0, size) -> frame pixels.
Landmark project(const RotRect &roi, float x, float y, float z, float size, float zNorm) {
  const Vec2 p = roiPoint(roi, x / size, y / size);
  return {p.x, p.y, z / size / zNorm * roi.w, 1, 1};
}

// Axis-aligned IoU of two rects' centre/size, ignoring rotation (AssociationNormRectCalculator).
float rectIou(const RotRect &a, const RotRect &b) {
  return iou(Box{a.cx - a.w / 2, a.cy - a.h / 2, a.w, a.h}, Box{b.cx - b.w / 2, b.cy - b.h / 2, b.w, b.h});
}

std::unique_ptr<Model> load(const std::filesystem::path &dir, const char *name, bool gpu) {
  const auto p = dir / (std::string(name) + ".onnx");
  if (!std::filesystem::exists(p))
    throw std::runtime_error("Model file missing: " + p.string() + ". Reinstall, or run build.bat.");
  return std::make_unique<Model>(p.wstring(), gpu);
}
}  // namespace

Models::Models(const std::filesystem::path &dir, bool gpu) : m_(std::make_unique<ModelSet>()) {
  m_->palm = load(dir, "palm_detector", gpu);
  m_->hand = load(dir, "hand_landmarks", gpu);
  m_->embedder = load(dir, "gesture_embedder", false);     // tiny MLPs: the CPU beats a GPU round trip
  m_->classifier = load(dir, "gesture_classifier", false);
  m_->faceDet = load(dir, "face_detector", gpu);
  m_->face = load(dir, "face_landmarks", gpu);
  m_->blend = load(dir, "face_blendshapes", gpu);
  m_->poseDet = load(dir, "pose_detector", gpu);
  m_->pose = load(dir, "pose_landmarks", gpu);
}

Models::~Models() = default;

Device Models::device() const {
  for (auto *m : {m_->palm.get(), m_->hand.get(), m_->faceDet.get(), m_->face.get(), m_->poseDet.get(), m_->pose.get()})
    if (m->device() != Device::DirectML) return Device::Cpu;
  return Device::DirectML;
}

// ---------------------------------------------------------------------------------------- hands

HandLandmarker::HandLandmarker(Models &m, int maxHands) : m_(m.set()), max_(maxHands), palm_(palmConfig()) {}

std::vector<Hand> HandLandmarker::process(const QImage &frame) {
  std::vector<RotRect> rois = tracks_;
  if (int(rois.size()) < max_) {
    const RotRect lb = letterboxRoi(frame.width(), frame.height(), 192, 192);
    Tensor in;
    cropToTensor(frame, lb, 192, 192, 0.f, 1.f, Border::Zero, in);
    auto out = m_.palm->run({&in});
    for (auto &d : palm_.decode(out[0].data.data(), out[1].data.data())) {
      // tensor units -> frame pixels (the letterbox has no rotation)
      const Vec2 a = roiPoint(lb, d.box.x, d.box.y), b = roiPoint(lb, d.box.right(), d.box.bottom());
      d.box = {a.x, a.y, b.x - a.x, b.y - a.y};
      for (auto &k : d.keypoints) k = roiPoint(lb, k.x, k.y);
      const RotRect r = palmToHandRoi(d);
      if (int(rois.size()) >= max_) break;
      if (std::none_of(rois.begin(), rois.end(), [&](const RotRect &t) { return rectIou(t, r) > kAssociation; }))
        rois.push_back(r);
    }
  }
  std::vector<Hand> hands;
  tracks_.clear();
  for (const RotRect &roi : rois) {
    auto h = landmarks(frame, roi);
    if (!h) continue;
    const RotRect next = handLandmarksToRoi(h->lm.data());
    // Two crops that converged on one hand: keep the first.
    if (std::any_of(tracks_.begin(), tracks_.end(), [&](const RotRect &t) { return rectIou(t, next) > kAssociation; }))
      continue;
    tracks_.push_back(next);
    classify(*h, frame.width(), frame.height());
    hands.push_back(*h);
  }
  return hands;
}

std::optional<Hand> HandLandmarker::landmarks(const QImage &frame, const RotRect &roi) {
  Tensor in;
  cropToTensor(frame, roi, 224, 224, 0.f, 1.f, Border::Replicate, in);
  auto out = m_.hand->run({&in});
  Hand h;
  h.presence = out[1].data[0];
  if (h.presence < kPresence) return std::nullopt;
  h.rightScore = out[2].data[0];
  h.roi = roi;
  for (int i = 0; i < 21; ++i) {
    const float *p = out[0].data.data() + i * 3;
    h.lm[i] = project(roi, p[0], p[1], p[2], 224, 0.4f);
    // WorldLandmarkProjectionCalculator: rotate by the crop's rotation, z as is.
    const float *w = out[3].data.data() + i * 3;
    const float c = std::cos(roi.rotation), s = std::sin(roi.rotation);
    h.world[i] = {w[0] * c - w[1] * s, w[0] * s + w[1] * c, w[2]};
  }
  return h;
}

void HandLandmarker::classify(Hand &h, int w, int ht) {
  // LandmarksToMatrixCalculator as the gesture graph configures it: normalized screen landmarks
  // aspect-corrected, then both sets made relative to the wrist and scaled to their own extent.
  // (MediaPipe seeds max with numeric_limits::min(); the wrist's own 0 makes that harmless.)
  auto normalize = [](std::array<Vec3, 21> p) {
    const Vec3 o = p[0];
    float x0 = std::numeric_limits<float>::max(), x1 = std::numeric_limits<float>::min();
    float y0 = x0, y1 = x1;
    for (auto &q : p) {
      q = {q.x - o.x, q.y - o.y, q.z - o.z};
      x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y);
    }
    const float scale = std::max(x1 - x0, y1 - y0) + 1e-5f;
    for (auto &q : p) q = {q.x / scale, q.y / scale, q.z / scale};
    return p;
  };
  const float maxDim = float(std::max(w, ht));
  std::array<Vec3, 21> screen;
  for (int i = 0; i < 21; ++i) {
    const float nx = h.lm[i].x / w, ny = h.lm[i].y / ht, nz = h.lm[i].z / w;
    screen[i] = {(nx - 0.5f) * (w / maxDim) + 0.5f, (ny - 0.5f) * (ht / maxDim) + 0.5f, nz};
  }
  screen = normalize(screen);
  const auto world = normalize(h.world);
  // HandednessToMatrix: GetRightHandScore, which is the raw output either way the top class falls.
  Tensor a{{}, {1, 21, 3}}, b{{h.rightScore}, {1, 1}}, c{{}, {1, 21, 3}};
  for (int i = 0; i < 21; ++i) {
    a.data.insert(a.data.end(), {screen[i].x, screen[i].y, screen[i].z});
    c.data.insert(c.data.end(), {world[i].x, world[i].y, world[i].z});
  }
  auto e = m_.embedder->run({&a, &b, &c});
  std::copy_n(e[0].data.begin(), 128, h.embedding.begin());
  Tensor emb{e[0].data, {1, 128}};
  auto p = m_.classifier->run({&emb});
  std::copy_n(p[0].data.begin(), int(Sign::Count), h.signs.begin());
}

// ----------------------------------------------------------------------------------------- face

FaceLandmarker::FaceLandmarker(Models &m) : m_(m.set()), det_(faceConfig()) {}

std::optional<Face> FaceLandmarker::process(const QImage &frame) {
  if (!track_) {
    const RotRect lb = letterboxRoi(frame.width(), frame.height(), 128, 128);
    Tensor in;
    cropToTensor(frame, lb, 128, 128, -1.f, 1.f, Border::Zero, in);
    auto out = m_.faceDet->run({&in});
    auto dets = det_.decode(out[0].data.data(), out[1].data.data());
    if (dets.empty()) return std::nullopt;
    auto &d = dets[0];
    const Vec2 a = roiPoint(lb, d.box.x, d.box.y), b = roiPoint(lb, d.box.right(), d.box.bottom());
    d.box = {a.x, a.y, b.x - a.x, b.y - a.y};
    for (auto &k : d.keypoints) k = roiPoint(lb, k.x, k.y);
    track_ = faceDetectionToRoi(d);
  }
  auto f = landmarks(frame, *track_);
  track_ = f ? std::optional(faceLandmarksToRoi(f->lm.data())) : std::nullopt;
  return f;
}

std::optional<Face> FaceLandmarker::landmarks(const QImage &frame, const RotRect &roi) {
  Tensor in;
  cropToTensor(frame, roi, 256, 256, 0.f, 1.f, Border::Replicate, in);
  auto out = m_.face->run({&in});
  Face f;
  f.presence = sigmoid(out[1].data[0]);   // a logit: the graph applies the sigmoid
  if (f.presence < kPresence) return std::nullopt;
  f.roi = roi;
  for (int i = 0; i < 478; ++i) {
    const float *p = out[0].data.data() + i * 3;
    f.lm[i] = project(roi, p[0], p[1], p[2], 256, 1.f);
  }
  Tensor pts{{}, {1, 146, 2}};
  pts.data.reserve(292);
  for (int idx : kBlendSubset) pts.data.insert(pts.data.end(), {f.lm[idx].x, f.lm[idx].y});
  auto b = m_.blend->run({&pts});
  std::copy_n(b[0].data.begin(), kBlendshapeCount, f.blend.begin());
  return f;
}

// ----------------------------------------------------------------------------------------- pose

PoseLandmarker::PoseLandmarker(Models &m) : m_(m.set()), det_(poseConfig()) {}

std::optional<Body> PoseLandmarker::process(const QImage &frame) {
  if (!track_) {
    const RotRect lb = letterboxRoi(frame.width(), frame.height(), 224, 224);
    Tensor in;
    cropToTensor(frame, lb, 224, 224, -1.f, 1.f, Border::Zero, in);
    auto out = m_.poseDet->run({&in});
    auto dets = det_.decode(out[0].data.data(), out[1].data.data());
    if (dets.empty()) return std::nullopt;
    auto &d = dets[0];
    for (auto &k : d.keypoints) k = roiPoint(lb, k.x, k.y);
    track_ = poseDetectionToRoi(d);
  }
  RotRect next;
  auto b = landmarks(frame, *track_, &next);
  track_ = b ? std::optional(next) : std::nullopt;
  return b;
}

std::optional<Body> PoseLandmarker::landmarks(const QImage &frame, const RotRect &roi, RotRect *next) {
  Tensor in;
  cropToTensor(frame, roi, 256, 256, 0.f, 1.f, Border::Replicate, in);
  auto out = m_.pose->run({&in});
  Body b;
  b.presence = out[1].data[0];            // already a probability (the model's own sigmoid)
  if (b.presence < kPresence) return std::nullopt;
  b.roi = roi;
  // RefineLandmarksFromHeatmapCalculator (kernel 7, min confidence 0.5), in crop space before the
  // projection: each point moves to the sigmoid-weighted centroid of its 64 x 64 heatmap channel
  // around it, where that channel is confident.
  float xy[39][2];
  const float *hm = out[3].data.data();
  constexpr int kHm = 64, kCh = 39, kOff = 3;
  for (int i = 0; i < kCh; ++i) {
    const float *p = out[0].data.data() + i * 5;
    float x = p[0] / 256, y = p[1] / 256;
    const int cc = int(x * kHm), cr = int(y * kHm);
    if (cc >= 0 && cc < kHm && cr >= 0 && cr < kHm) {
      float sum = 0, wc = 0, wr = 0, best = 0;
      for (int r = std::max(0, cr - kOff); r < std::min(kHm, cr + kOff + 1); ++r)
        for (int c = std::max(0, cc - kOff); c < std::min(kHm, cc + kOff + 1); ++c) {
          const float conf = sigmoid(hm[(r * kHm + c) * kCh + i]);
          sum += conf;
          best = std::max(best, conf);
          wc += c * conf;
          wr += r * conf;
        }
      if (best >= 0.5f && sum > 0) {
        x = wc / kHm / sum;
        y = wr / kHm / sum;
      }
    }
    xy[i][0] = x * 256;
    xy[i][1] = y * 256;
  }
  Landmark aux[2];
  for (int i = 0; i < 35; ++i) {
    const float *p = out[0].data.data() + i * 5;
    // z divisor 1: the pose graph builds its TensorsToLandmarks with normalize = false, so its 0.4
    // (kLandmarksNormalizeZ) is never applied; only the hand graph uses it.
    Landmark l = project(roi, xy[i][0], xy[i][1], p[2], 256, 1.f);
    l.visibility = sigmoid(p[3]);
    l.presence = sigmoid(p[4]);
    if (i < 33) b.lm[i] = l;
    else aux[i - 33] = l;
  }
  *next = poseLandmarksToRoi(aux);
  return b;
}

}  // namespace racg::vision
