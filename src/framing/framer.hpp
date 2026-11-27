// Framing on one picture: which part of it the output shows. Everything is a crop of the
// (mirrored) picture, eased over time; the zoom steps use the webcam's own zoom instead when it
// has one (the action layer decides that). The moves are always on while their control is:
//   pinch zoom: a pinch grabs the picture at once (kGrab steadies it against a flicker); pushing
//     toward the camera while pinched zooms in, pulling back zooms out (the hand's size on screen
//     is its distance). Let go and pinch again to keep going, hand over hand, like a rope. While
//     follow is framing a face (or its gesture is held) it sets how close follow is instead.
//   pinch box: both hands pinch within 0.5 s; the box between them follows them, and stays as the
//     zoom when they let go.
//   zoom on what I hold: one pinch held still 0.7 s; the hand and what it holds fill the frame
//     until the pinch lets go. It reads the same move as pinch zoom, so Controls keeps one of them.
//   follow face: the face kept centred at its own closeness (face heights per picture height);
//     eases back after 2 s without a face.
//   the follow chord: while follow's gesture is held, a pinch zoom from the other hand sets how
//     close follow is, and letting go of the gesture then leaves follow on instead of toggling it.
// Targets are set from observations (UI thread); crop(t) is read by the composing thread.
#pragma once

#include "vision/observation.hpp"

#include <QRectF>
#include <QString>

#include <mutex>
#include <vector>

namespace racg::framing {

constexpr float kPresets[] = {1.0f, 1.5f, 2.0f, 3.0f};
constexpr double kBoxWindow = 0.5, kHoldPinch = 0.7, kFaceLost = 2.0, kGrab = 0.12;
constexpr double kMaxZoom = 5.0;
constexpr double kFollowSpanDefault = 1.8, kFollowSpanMin = 1.1, kFollowSpanMax = 6.0;

class Framer {
 public:
  struct Moves {
    bool pinchZoom = true, box = true, hold = false;
  };
  void setMoves(Moves m);
  void setAspect(float w_over_h);   // the output's shape

  // Zoom steps: to the next preset above or below the zoom now; returns its index.
  int stepPreset(int delta);
  int preset() const;               // the preset nearest the zoom now
  void setPreset(int i);

  void setFollow(bool on);
  bool followOn() const;
  void setFollowSpan(double faceHeights);
  double followSpan() const;
  bool takeSpanChanged();           // true once after a pinch set how close follow is
  void beginChord();
  bool endChord();                  // returns whether follow is on afterwards
  void reset();                     // the stop sign: the whole picture, follow off

  // Each observation (UI thread), with its time in seconds.
  void update(const vision::Observation &obs, double now);
  // The crop for a moment, as fractions of the picture. Any thread.
  QRectF crop(double now);
  // The target the crop is easing toward (the link sends this; the plugin eases it).
  QRectF target() const;

  // What the detection overlay shows for the moves: a ring and words at a point (analysed pixels).
  struct Mark {
    Vec2 at;
    float ring = 0;
    QString text;
  };
  std::vector<Mark> marks() const;

 private:
  QRectF aspectBox(QPointF centre, double height) const;   // fractions, clamped inside 0..1
  mutable std::mutex mu_;
  Moves moves_;
  float aspect_ = 16.f / 9;

  double zoom_ = 1;                  // the manual zoom (steps, pinch zoom, a box let go)
  QPointF centre_{0.5, 0.5};         // and where it looks
  bool follow_ = false, chord_ = false, chordAdjusted_ = false, spanChanged_ = false;
  double span_ = kFollowSpanDefault, faceH_ = 0.3;
  QRectF followCrop_, holdCrop_, boxCrop_;
  bool followSet_ = false, holdSet_ = false, boxing_ = false;
  double pinch_[2] = {-1, -1};       // when each hand started pinching
  double holdSince_ = -1, faceSeen_ = -1;
  Vec2 holdAt_;

  struct Grab {
    bool arming = false, held = false, forFollow = false;
    double since = -1, lostAt = -1;
    Vec2 at0, at;
    double size = 0, size0 = 0, base = 1;
  } grab_;

  std::vector<Mark> marks_;
  QRectF target_{0, 0, 1, 1}, cur_{0, 0, 1, 1};
  double curAt_ = -1;
};

}  // namespace racg::framing
