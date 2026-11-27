#include "gestures/engine.hpp"

#include <algorithm>
#include <bit>

namespace racg::gestures {

// The strict two-hand guard (Engine::update): how weakly the other hand may show the same sign and
// still count as a two-hand pose.
constexpr float kPairSign = 0.35f;

Engine::Engine(Library &lib) : lib_(lib) {}

const GestureState *Engine::state(const QString &id) const {
  auto it = runs_.find(id);
  return it == runs_.end() ? nullptr : &it->second.st;
}

std::vector<std::pair<const GestureDef *, GestureState>> Engine::visible() const {
  std::vector<std::pair<const GestureDef *, GestureState>> out;
  for (const auto &g : lib_.gestures) {
    auto it = runs_.find(g.id);
    if (it == runs_.end()) continue;
    const GestureState &s = it->second.st;
    if (s.matching || s.waitingConfirm || (s.active && g.ends == Ends::Held)) out.push_back({&g, s});
  }
  return out;
}

void Engine::forget(const QString &gestureId) {
  auto it = runs_.find(gestureId);
  if (it != runs_.end()) it->second.st.active = false;
}

std::vector<Command> Engine::endAll(const QString &reason) {
  std::vector<Command> out;
  for (const auto &g : lib_.gestures) {
    auto it = runs_.find(g.id);
    if (it == runs_.end() || !it->second.st.active) continue;
    it->second.st.active = false;
    out.push_back({Command::End, g.id, g.action, reason});
  }
  return out;
}

void Engine::reset() {
  runs_.clear();
  motion_.reset();
  last_ = -1;
  stopSince_ = -1;
  stopDone_ = false;
}

std::vector<Command> Engine::update(const vision::Observation &obs, const Baseline &base, double now, bool dry) {
  std::vector<Command> out;
  const double dt = last_ < 0 ? 1.0 / 15 : std::clamp(now - last_, 1e-3, 0.5);
  last_ = now;
  ready_ = evaluate(obs, base, motion_, float(dt));
  if (dry) {
    for (const auto &g : lib_.gestures) {
      Run &r = runs_[g.id];
      Match m;
      switch (g.kind) {
        case Kind::Ready: m.score = ready_.score[g.ref]; break;
        case Kind::Sign: m = matchSign(g.sign(), obs); break;
        case Kind::Mine: m = matchCustom(g.takes, g.parts, obs, base); break;
      }
      r.st.pct = m.score * 60;
      r.st.hands = m.hands;
      r.st.matching = false;
      r.st.armed = 0;
      r.st.waitingConfirm = false;
      r.seenSince = -1;
    }
    stopProgress_ = 0;
    return out;
  }

  // The stop sign: held for stopHold, it ends everything that listens to it, once per hold. While
  // it's up nothing else fires (both palms would otherwise read as "Open palm" or "Hand up").
  const bool stopUp = stopSignScore(cfg_.stopSign, obs) >= 1;
  if (stopUp) {
    if (stopSince_ < 0) stopSince_ = now - dt;
    stopProgress_ = float(std::min(1.0, (now - stopSince_) / cfg_.stopHold));
    if (!stopDone_ && now - stopSince_ >= cfg_.stopHold) {
      stopDone_ = true;
      for (const auto &g : lib_.gestures) {
        Run &r = runs_[g.id];
        if (r.st.active && g.stop) {
          r.st.active = false;
          out.push_back({Command::End, g.id, g.action, "stop sign"});
        }
      }
    }
  } else {
    stopSince_ = -1;
    stopDone_ = false;
    stopProgress_ = 0;
  }

  auto allowed = [&](const GestureDef &g) {
    return g.on && !g.action.isEmpty() && !stopUp && (!paused_ || g.action == kPauseAction);
  };

  // itsgiving's decide(): the first candidate, in its order, that passes its own line.
  int raw = -1;
  unsigned rawHands = 0;
  for (const auto &c : ready_.order) {
    const GestureDef *g = lib_.find(QString("ready:") + reactionId(c.r));
    if (g && allowed(*g) && c.score * 60 >= g->sens) {
      raw = int(c.r);
      rawHands = c.hands;
      break;
    }
  }

  // Pass 1: every gesture's match.
  for (const auto &g : lib_.gestures) {
    Run &r = runs_[g.id];
    Match m;
    switch (g.kind) {
      case Kind::Ready:
        m.score = ready_.score[g.ref];
        m.hands = raw == g.ref ? rawHands : 0;
        break;
      case Kind::Sign: m = matchSign(g.sign(), obs); break;
      case Kind::Mine: m = matchCustom(g.takes, g.parts, obs, base); break;
    }
    r.st.pct = m.score * 60;
    r.st.hands = m.hands;
    r.st.matching = allowed(g) && r.st.pct >= g.sens && (g.kind != Kind::Ready || raw == g.ref);
  }

  // The confirm sign is a confirmation, not a gesture, while something is waiting on it.
  bool someoneNeedsConfirm = false;
  for (const auto &g : lib_.gestures)
    if (g.confirm && runs_[g.id].st.matching && std::popcount(runs_[g.id].st.hands) < 2) someoneNeedsConfirm = true;
  auto confirmShown = [&](unsigned used) {
    for (size_t i = 0; i < obs.hands.size(); ++i)
      if (!(used & (1u << i)) && obs.hands[i].signs[int(cfg_.confirmSign)] >= 0.5f) return true;
    return false;
  };

  // The strict two-hand guard. Two hands making one pose (the stop sign, both fists, both palms,
  // a two-hand gesture, the pinch box) are never a one-hand gesture: "Open palm" must not turn the
  // camera off when both palms come up, nor a one-hand "Fist" fire for both fists. A one-hand
  // gesture caught in such a pose stays quiet until its hand lets go, so lowering one hand after
  // the pose doesn't fire it either.
  const unsigned all = obs.hands.size() >= 2 ? 3u : 0u;
  unsigned pair = handsBusy_ || stopUp ? all : 0u;
  if (cfg_.stopSign == StopSign::BothPalms && stopSignScore(StopSign::BothPalms, obs) >= 0.7f) pair = all;
  for (const auto &g : lib_.gestures) {
    const GestureState &s = runs_[g.id].st;
    if (g.on && !g.action.isEmpty() && std::popcount(s.hands) >= 2 && s.pct >= g.sens) pair |= s.hands;
  }
  // The other hand making the same sign, even weakly: the second hand is often a frame behind.
  auto otherShows = [&](vision::Sign sign, unsigned used) {
    for (size_t i = 0; i < obs.hands.size() && i < 32; ++i)
      if (!(used & (1u << i)) && !vision::isPinch(obs.hands[i]) && obs.hands[i].signs[int(sign)] >= kPairSign) return true;
    return false;
  };
  for (const auto &g : lib_.gestures) {
    Run &r = runs_[g.id];
    GestureState &s = r.st;
    const bool over = s.pct >= g.sens;
    if (over) r.lastRaw = now;
    else if (now - r.lastRaw > kHoldSeconds) r.claimed = false;
    if (over && std::popcount(s.hands) < 2 &&
        (stopUp || (s.hands & pair) || (g.kind == Kind::Sign && otherShows(g.sign(), s.hands))))
      r.claimed = true;
    if (r.claimed) s.matching = false;
  }

  // Pass 2: hold, confirm, fire, end.
  for (const auto &g : lib_.gestures) {
    Run &r = runs_[g.id];
    GestureState &s = r.st;
    if (someoneNeedsConfirm && g.kind == Kind::Sign && g.sign() == cfg_.confirmSign) s.matching = false;
    const bool oneShot = cfg_.oneShot(g.action);
    if (s.matching) {
      // itsgiving counts frames: the first frame it's seen already counts as one frame held.
      if (r.seenSince < 0) r.seenSince = now - dt;
      r.lastMatch = now;
      const double held = now - r.seenSince;
      s.armed = float(std::min(1.0, held / std::max(g.hold, 1e-3f)));
      if (held + 1e-4 >= g.hold) {
        const bool needConfirm = g.confirm && std::popcount(s.hands) < 2;
        const bool ok = !needConfirm || confirmShown(s.hands);
        s.waitingConfirm = !ok && !r.latched;
        if (ok && !r.latched) {
          r.latched = true;
          if (oneShot) {
            out.push_back({Command::Start, g.id, g.action, "gesture"});
          } else if (s.active && g.ends == Ends::Again) {
            s.active = false;
            out.push_back({Command::End, g.id, g.action, "again"});
          } else if (!s.active) {
            s.active = true;
            r.until = now + g.secs;
            out.push_back({Command::Start, g.id, g.action, "gesture"});
          } else if (g.ends == Ends::After) {
            r.until = now + g.secs;   // shown again while it runs: the time starts over
          }
        }
      }
    } else {
      r.seenSince = -1;
      s.armed = 0;
      s.waitingConfirm = false;
      const bool gone = now - r.lastMatch > kHoldSeconds;
      if (gone) r.latched = false;   // let go (the linger also stops a flicker re-firing it)
      if (s.active && g.ends == Ends::Held && gone) {
        s.active = false;
        out.push_back({Command::End, g.id, g.action, "let go"});
      }
    }
    if (s.active && g.ends == Ends::After && now >= r.until) {
      s.active = false;
      out.push_back({Command::End, g.id, g.action, "time"});
    }
  }
  return out;
}

}  // namespace racg::gestures
