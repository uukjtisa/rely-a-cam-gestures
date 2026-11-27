// The three timed things the editor runs on the live picture: the 7 s neutral-face calibration,
// recording a take (3-2-1, then hold 2 s), and testing (a live match %, and a 10 s "move normally"
// check that counts how often the gesture would have fired by mistake).
#pragma once

#include "gestures/engine.hpp"
#include "gestures/itsgiving.hpp"
#include "gestures/library.hpp"
#include "gestures/matcher.hpp"

#include <QImage>
#include <QString>

#include <optional>
#include <vector>

namespace racg::gestures {

class Calibrator {
 public:
  void start(double now);
  void cancel() { running_ = false; }
  bool running() const { return running_; }
  // Feeds a frame. Returns true once the time is up; then ok() says whether it worked.
  bool add(const vision::Observation &obs, double now);
  float progress(double now) const;
  float secondsLeft(double now) const;
  int samples() const { return col_.count(); }
  bool faceNow() const { return faceNow_; }

  bool ok() const { return result.has_value(); }
  std::optional<Baseline> result;
  QString error;
  std::vector<QString> warnings;

 private:
  bool running_ = false, faceNow_ = false;
  int seen_ = 0;
  double t0_ = 0;
  Collector col_;
};

class Recorder {
 public:
  static constexpr double kCountdown = 3.0, kHold = 2.0;
  enum class Phase { Idle, Countdown, Hold };
  void start(const QString &gestureId, double now);
  void cancel() { phase_ = Phase::Idle; }
  Phase phase() const { return phase_; }
  int countdown(double now) const;       // 3, 2, 1
  float holdProgress(double now) const;  // 0..1
  float holdSeconds(double now) const;
  // Returns true when the take is finished; then `take` (or `error`) and `picture` are set.
  bool add(const vision::Observation &obs, const Baseline &base, double now);

  QString gestureId;
  std::optional<Take> take;
  QImage picture;
  QString error;

 private:
  Phase phase_ = Phase::Idle;
  double t0_ = 0;
  TakeBuilder builder_;
};

class Tester {
 public:
  static constexpr double kMisfireSeconds = 10.0;
  enum class Mode { Off, Live, Misfire };
  void startLive(const QString &gestureId);
  void startMisfire(const QString &gestureId, double now);
  void stop() { mode_ = Mode::Off; }
  Mode mode() const { return mode_; }
  const QString &gestureId() const { return id_; }
  // Each frame, with the gesture and the engine's state for it. Returns true when a misfire check ends.
  bool add(const GestureDef &g, const GestureState *s, double now);
  float pct() const { return pct_; }
  int fires() const { return fires_; }
  float secondsLeft(double now) const;
  float bestLivePct() const { return best_; }
  QString result;   // the sentence the editor shows after a misfire check

 private:
  Mode mode_ = Mode::Off;
  QString id_;
  double t0_ = 0, overSince_ = -1, last_ = -1;
  bool counted_ = false;
  float pct_ = 0, best_ = 0;
  int fires_ = 0;
};

}  // namespace racg::gestures
