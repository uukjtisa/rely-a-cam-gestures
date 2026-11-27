// The gesture engine: each detection frame, every gesture's match %, then the rules that turn a
// match into an action: its line (sensitivity), its hold time, the confirm sign from the other hand,
// the stop sign, how it ends, and pause. It only says "start" and "end"; the action layer does them.
//
// itsgiving's reactions stay exclusive the way decide() makes them: in its order, the first one over
// its own line is the only reaction that matches this frame.
#pragma once

#include "gestures/itsgiving.hpp"
#include "gestures/library.hpp"
#include "gestures/matcher.hpp"

#include <QString>

#include <functional>
#include <map>
#include <vector>

namespace racg::gestures {

struct EngineConfig {
  vision::Sign confirmSign = vision::Sign::ThumbUp;
  StopSign stopSign = StopSign::BothPalms;
  float stopHold = 0.3f;   // seconds: the stop sign is deliberate, so it acts at once
  // Actions that just happen (a snapshot, a scene switch) have no end, so they never stay active.
  std::function<bool(const QString &action)> oneShot = [](const QString &) { return false; };
};

struct GestureState {
  float pct = 0;               // 60 x score, uncapped
  bool matching = false;       // over its line this frame, and allowed to fire
  float armed = 0;             // 0..1 of its hold time
  bool waitingConfirm = false; // held long enough, waiting for the confirm sign
  bool active = false;         // its action started and hasn't ended
  unsigned hands = 0;          // the observation's hands it matched with
};

struct Command {
  enum Type { Start, End } type;
  QString gesture, action, reason;
};

class Engine {
 public:
  explicit Engine(Library &lib);
  void setConfig(EngineConfig c) { cfg_ = std::move(c); }
  const EngineConfig &config() const { return cfg_; }

  // While paused only a gesture whose action is the pause toggle can fire.
  void setPaused(bool p) { paused_ = p; }
  // Both hands are framing (the pinch box, or aiming it): no hand gesture may fire.
  void setHandsBusy(bool b) { handsBusy_ = b; }
  bool paused() const { return paused_; }

  // `dry`: only the match % (Test, recording, calibration): nothing arms, fires or ends.
  std::vector<Command> update(const vision::Observation &obs, const Baseline &base, double now, bool dry = false);

  const GestureState *state(const QString &id) const;
  // What to draw: gestures matching, waiting for confirm, or running "while held".
  std::vector<std::pair<const GestureDef *, GestureState>> visible() const;
  float stopProgress() const { return stopProgress_; }
  const ReadyEval &ready() const { return ready_; }

  void forget(const QString &gestureId);                      // its action was ended elsewhere (undo)
  std::vector<Command> endAll(const QString &reason);         // pause, camera change
  void reset();                                               // the library was replaced

  static constexpr const char *kPauseAction = "gestures.pause";

 private:
  struct Run {
    double seenSince = -1, lastMatch = -1e9, until = 0;
    bool latched = false;
    double lastRaw = -1e9;   // last frame it was over its line, allowed or not
    bool claimed = false;    // caught in a two-hand pose: quiet until its hand lets go
    GestureState st;
  };
  Library &lib_;
  EngineConfig cfg_;
  Motion motion_;
  ReadyEval ready_;
  std::map<QString, Run> runs_;
  double last_ = -1, stopSince_ = -1;
  bool stopDone_ = false, paused_ = false, handsBusy_ = false;
  float stopProgress_ = 0;
};

}  // namespace racg::gestures
