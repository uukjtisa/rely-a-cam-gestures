#include "gestures/sessions.hpp"

#include <QDateTime>

#include <algorithm>
#include <cmath>

namespace racg::gestures {

// ---- calibration ---------------------------------------------------------------------------------

void Calibrator::start(double now) {
  running_ = true;
  t0_ = now;
  seen_ = 0;
  col_ = Collector();
  result.reset();
  error.clear();
  warnings.clear();
}

float Calibrator::progress(double now) const { return float(std::clamp((now - t0_) / kCalibSeconds, 0.0, 1.0)); }
float Calibrator::secondsLeft(double now) const { return float(std::max(0.0, kCalibSeconds - (now - t0_))); }

bool Calibrator::add(const vision::Observation &obs, double now) {
  if (!running_) return false;
  const double elapsed = now - t0_;
  faceNow_ = obs.face.has_value();
  if (elapsed <= kCalibSeconds) {
    if (obs.face) {
      ++seen_;
      if (elapsed > kCalibWarmup) col_.add(*obs.face, obs.body ? &*obs.body : nullptr);
    }
    return false;
  }
  running_ = false;
  if (col_.count() < kCalibMinSamples) {
    error = QString("Only %1 usable frames%2. Light your face from the front, sit head-and-shoulders in "
                    "frame, and try again.")
                .arg(col_.count())
                .arg(seen_ ? "" : ": your face was never found");
    return true;
  }
  result = col_.finish(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
  warnings = calibrationWarnings(*result);
  return true;
}

// ---- recording -----------------------------------------------------------------------------------

void Recorder::start(const QString &id, double now) {
  gestureId = id;
  phase_ = Phase::Countdown;
  t0_ = now;
  take.reset();
  picture = {};
  error.clear();
  builder_ = TakeBuilder();
}

int Recorder::countdown(double now) const { return std::clamp(3 - int(std::floor(now - t0_)), 1, 3); }
float Recorder::holdSeconds(double now) const {
  return phase_ == Phase::Hold ? float(std::clamp(now - t0_, 0.0, kHold)) : 0.f;
}
float Recorder::holdProgress(double now) const { return holdSeconds(now) / float(kHold); }

bool Recorder::add(const vision::Observation &obs, const Baseline &base, double now) {
  if (phase_ == Phase::Countdown) {
    if (now - t0_ >= kCountdown) {
      phase_ = Phase::Hold;
      t0_ = now;
    }
    return false;
  }
  if (phase_ != Phase::Hold) return false;
  builder_.add(obs, base);
  if (now - t0_ < kHold) return false;
  phase_ = Phase::Idle;
  take = builder_.finish();
  picture = builder_.picture();
  if (!take) error = "Nothing was seen during the take: no hand, face or body. Step into the picture and record again.";
  return true;
}

// ---- testing -------------------------------------------------------------------------------------

void Tester::startLive(const QString &id) {
  mode_ = Mode::Live;
  id_ = id;
  best_ = 0;
  result.clear();
}

void Tester::startMisfire(const QString &id, double now) {
  mode_ = Mode::Misfire;
  id_ = id;
  t0_ = now;
  fires_ = 0;
  overSince_ = -1;
  last_ = -1;
  counted_ = false;
  result.clear();
}

float Tester::secondsLeft(double now) const {
  return mode_ == Mode::Misfire ? float(std::max(0.0, kMisfireSeconds - (now - t0_))) : 0.f;
}

bool Tester::add(const GestureDef &g, const GestureState *s, double now) {
  if (mode_ == Mode::Off || g.id != id_) return false;
  pct_ = s ? s->pct : 0;
  if (mode_ == Mode::Live) {
    best_ = std::max(best_, pct_);
    return false;
  }
  // Would it have fired? Over its line for its hold time, counted once per time it's made.
  const double dt = last_ < 0 ? 1.0 / 15 : now - last_;
  last_ = now;
  if (pct_ >= g.sens) {
    if (overSince_ < 0) overSince_ = now - dt;
    if (!counted_ && now - overSince_ + 1e-4 >= g.hold) {
      ++fires_;
      counted_ = true;
    }
  } else {
    overSince_ = -1;
    counted_ = false;
  }
  if (now - t0_ < kMisfireSeconds) return false;
  mode_ = Mode::Off;
  result = fires_ == 0
               ? QString("Misfire check: no false fires in 10 s. Good to use.")
               : QString("Misfire check: %1 false fire%2 in 10 s. Raise the line or the hold time, or record "
                         "another take.")
                     .arg(fires_)
                     .arg(fires_ == 1 ? "" : "s");
  return true;
}

}  // namespace racg::gestures
