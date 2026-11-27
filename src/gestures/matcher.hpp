// Your own gestures and Google's hand signs, scored on the same scale as itsgiving's reactions:
// score 1 sits on the default line (60 %), match % = 60 x score.
//
// A recorded take keeps, per part: Google's 128-number gesture embedding of each hand (cosine
// similarity; the vision test shows the same sign on different hands at 0.72 and different signs at
// 0.40 at most, so the line sits at 0.65) with the palm's place next to the face; the face's
// blendshapes as sigma above your calibrated neutral; and the arm shape. A gesture matches when
// every ticked part does; with several takes, the best take counts.
#pragma once

#include "gestures/itsgiving.hpp"
#include "vision/observation.hpp"

#include <QImage>
#include <QJsonObject>

#include <array>
#include <optional>
#include <vector>

namespace racg::gestures {

struct Parts {
  bool leftHand = false, rightHand = false, face = false, body = false;
  bool any() const { return leftHand || rightHand || face || body; }
  bool bothHands() const { return leftHand && rightHand; }
  QJsonObject toJson() const;
  static Parts fromJson(const QJsonObject &o);
};

struct Take {
  struct HandShape {
    bool seen = false;
    std::array<float, 128> embedding{};   // unit length
    bool placed = false;                  // a face was seen too, so `place` means something
    Vec2 place;                           // palm minus nose, in face widths
  };
  HandShape hand[2];                      // 0 = left, 1 = right (the person's own)
  std::vector<float> face;                // 52 z-scores (capped at +-8); empty without a face
  std::vector<float> arms;                // armVector(); empty without a body
  QString thumb;                          // its picture's file name in the library's takes folder
  QJsonObject toJson() const;
  static Take fromJson(const QJsonObject &o);
};

// Collects one take over the 2 s hold.
class TakeBuilder {
 public:
  void add(const vision::Observation &obs, const Baseline &base);
  int frames() const { return n_; }
  std::optional<Take> finish() const;   // nothing if no hand, face or body was ever seen
  QImage picture() const { return pic_; }   // a frame from the middle of the hold

 private:
  int n_ = 0, nHand_[2] = {0, 0}, nPlaced_[2] = {0, 0}, nFace_ = 0, nArms_ = 0;
  std::array<double, 128> emb_[2]{};
  Vec2 place_[2];
  std::vector<double> face_, arms_;
  QImage pic_;
};

// The parts a set of takes changed from the neutral: the hands it saw, the face if an expression
// moved at least 4 sigma (gaze excluded), the body if the arms moved clearly from the neutral pose.
Parts defaultParts(const std::vector<Take> &takes, const Baseline &base);

struct Match {
  float score = 0;
  unsigned hands = 0;   // bit i: the observation's hand i is part of it
};
Match matchCustom(const std::vector<Take> &takes, const Parts &parts, const vision::Observation &obs,
                  const Baseline &base);
Match matchSign(vision::Sign sign, const vision::Observation &obs);

// The stop signs (Settings): both palms open, or arms crossed over the chest.
enum class StopSign { BothPalms, CrossedArms };
float stopSignScore(StopSign s, const vision::Observation &obs);

// The face's 52 blendshapes as capped sigma above the baseline (the face part's feature).
std::vector<float> faceZ(const vision::Face &f, const Baseline &base);

}  // namespace racg::gestures
