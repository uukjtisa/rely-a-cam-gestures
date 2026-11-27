// What the detector saw in one frame. Everything is in pixels of the analysed frame (`size`),
// which is the camera picture scaled to fit 640 x 360 and mirrored like the preview.
#pragma once

#include "common/geometry.hpp"

#include <QImage>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace racg::vision {

// Google's canned hand signs, in the classifier's output order.
enum class Sign { None, ClosedFist, OpenPalm, PointingUp, ThumbDown, ThumbUp, Victory, ILoveYou, Count };
const char *signId(Sign s);      // "Closed_Fist" ... (MediaPipe's labels)
const char *signLabel(Sign s);   // "Fist" ... (what the window shows)

struct Hand {
  std::array<Landmark, 21> lm{};
  std::array<Vec3, 21> world{};          // metres, hand-centred (MediaPipe's world landmarks)
  float presence = 0;
  // The model's handedness output: P(right hand) as MediaPipe labels it (its label map puts "Right"
  // at index 0), which assumes a mirrored, selfie-style picture: the person's own right hand.
  float rightScore = 0;
  bool isLeft() const { return rightScore < 0.5f; }
  RotRect roi;                           // the crop it was found in
  std::array<float, 128> embedding{};    // Google's gesture embedding (L2 as the model gives it)
  std::array<float, int(Sign::Count)> signs{};   // classifier probabilities
  Sign sign() const;                     // the best non-None sign at >= 0.5, else None
};

constexpr int kBlendshapeCount = 52;
const char *blendshapeName(int i);       // MediaPipe's order: "_neutral", "browDownLeft", ...
int blendshapeIndex(const std::string &name);   // -1 if unknown

// A pinch: thumb tip and index tip together (within `touch` hand sizes), with the index reaching
// out of the palm. In a fist the two tips also meet, curled into it, so the reach (in the 3D
// world landmarks, which don't foreshorten) tells them apart.
inline bool isPinch(const Hand &h, float touch = 0.25f) {
  const float size = dist(h.lm[0].xy(), h.lm[9].xy());   // wrist to middle knuckle
  if (size <= 1 || dist(h.lm[4].xy(), h.lm[8].xy()) >= size * touch) return false;
  auto far = [](Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
  const float palm = far(h.world[9], h.world[0]);
  return palm <= 0 || far(h.world[8], h.world[0]) > palm * 1.15f;
}

struct Face {
  std::array<Landmark, 478> lm{};
  std::array<float, kBlendshapeCount> blend{};
  float presence = 0;
  RotRect roi;
};

struct Body {
  std::array<Landmark, 33> lm{};         // visibility/presence filled
  float presence = 0;
  RotRect roi;
};

struct Observation {
  int64_t frameId = 0;
  int64_t timeMs = 0;                    // monotonic, when the frame was captured
  int width = 0, height = 0;             // analysed frame size
  QImage frame;                          // the analysed frame itself (shared, read-only)
  // The camera picture it was made from: analysed = full scaled by 1/fullScale, then mirrored when
  // `mirrored`. toFull() maps an analysed point back.
  QImage full;
  float fullScale = 1;
  bool mirrored = true;
  Vec2 toFull(Vec2 p) const { return {(mirrored ? width - p.x : p.x) * fullScale, p.y * fullScale}; }
  std::optional<Face> face;
  std::vector<Hand> hands;               // at most two
  std::optional<Body> body;
  float mouthPink = 0;                   // itsgiving's tongue pixels (gestures::mouthPink), 0 without a face
  float ms = 0;                          // how long the models took
};

}  // namespace racg::vision
