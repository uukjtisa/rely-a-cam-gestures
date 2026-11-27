// Framing on one picture: which part of it the output shows. Everything is a crop of the
// (mirrored) picture, eased over time; the zoom presets use the webcam's own zoom instead when it
// has one, and the phone's zoom when Rely-A-Cam is attached (the action layer decides that).
//   zoom presets: 1x, 1.5x, 2x, 3x, centred (on the face while follow face is on).
//   two pinches draw a box: both hands pinch within 0.5 s; the box between them fills the frame.
//   zoom on what I hold: one pinch held 0.7 s; the hand and what it holds fill the frame.
//   follow face: shoulders up, kept centred; eases back after 2 s without a face.
// Targets are set from observations (UI thread); crop(t) is read by the composing thread.
#pragma once

#include "vision/observation.hpp"

#include <QRectF>

#include <mutex>

namespace racg::framing {

constexpr float kPresets[] = {1.0f, 1.5f, 2.0f, 3.0f};
constexpr double kBoxWindow = 0.5, kHoldPinch = 0.7, kFaceLost = 2.0;

class Framer {
 public:
  void setAspect(float w_over_h);   // the output's shape
  // Presets: the index into kPresets; returns the new index.
  int stepPreset(int delta);
  int preset() const;
  void setPreset(int i);
  void setBox(bool on);
  void setHold(bool on);
  void setFollow(bool on);
  bool boxOn() const;
  bool holdOn() const;
  bool followOn() const;
  void reset();

  // Each observation (UI thread), with its time in seconds.
  void update(const vision::Observation &obs, double now);
  // The crop for a moment, as fractions of the picture. Any thread.
  QRectF crop(double now);
  // The target the crop is easing toward (the link sends this; the plugin eases it).
  QRectF target() const;

 private:
  QRectF aspectBox(QPointF centre, double height) const;   // fractions, clamped inside 0..1
  mutable std::mutex mu_;
  float aspect_ = 16.f / 9;
  int preset_ = 0;
  bool box_ = false, hold_ = false, follow_ = false;
  QRectF boxCrop_, holdCrop_, followCrop_;
  bool boxSet_ = false, holdSet_ = false, followSet_ = false;
  double pinch_[2] = {-1, -1};       // when each hand started pinching
  double holdSince_ = -1, faceSeen_ = -1;
  QRectF target_{0, 0, 1, 1}, cur_{0, 0, 1, 1};
  double curAt_ = -1;
};

// A hand is pinching: thumb tip and index tip together (under a quarter of the hand's size).
bool pinching(const vision::Hand &h);

}  // namespace racg::framing
