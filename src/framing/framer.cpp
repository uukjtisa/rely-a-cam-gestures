#include "framing/framer.hpp"

#include <algorithm>
#include <cmath>

namespace racg::framing {

namespace {
constexpr double kEase = 0.25;        // seconds: the crop covers ~63 % of the way in this time
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));
constexpr double kGain = 2.2;         // zoom = (hand size now / at the grab) ^ kGain
constexpr double kStill = 0.35;       // a grab restarts if the pinch moves this many hand sizes
constexpr double kLetGo = 0.15;       // seconds without the pinch before a grab lets go
constexpr double kSizeSmooth = 0.4;   // the hand size's smoothing (new sample's weight)
constexpr double kDeadband = 0.05;    // a grabbed hand's size can wobble this much before it zooms

double span(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// The hand's size on screen, steadier than one bone: wrist to middle knuckle and across the palm.
double handSize(const vision::Hand &h) {
  return (span(h.lm[0].xy(), h.lm[9].xy()) + span(h.lm[5].xy(), h.lm[17].xy())) / 2;
}
Vec2 pinchPoint(const vision::Hand &h) { return (h.lm[4].xy() + h.lm[8].xy()) / 2; }
}  // namespace

void Framer::setMoves(Moves m) {
  std::lock_guard l(mu_);
  if (!m.box) boxing_ = false;
  if (!m.hold) holdSet_ = false;
  if (!m.pinchZoom) grab_ = {};
  moves_ = m;
}

void Framer::setAspect(float a) {
  std::lock_guard l(mu_);
  if (a > 0.1f) aspect_ = a;
}

int Framer::stepPreset(int delta) {
  std::lock_guard l(mu_);
  if (follow_) {   // while following, a step moves how close it frames the face
    span_ = std::clamp(delta > 0 ? span_ / 1.25 : span_ * 1.25, kFollowSpanMin, kFollowSpanMax);
    spanChanged_ = true;
  }
  int i = 0;
  if (delta > 0) {
    i = kPresetCount - 1;
    for (int k = 0; k < kPresetCount; ++k)
      if (kPresets[k] > zoom_ + 0.01) {
        i = k;
        break;
      }
  } else {
    for (int k = kPresetCount - 1; k >= 0; --k)
      if (kPresets[k] < zoom_ - 0.01) {
        i = k;
        break;
      }
  }
  zoom_ = kPresets[i];
  if (zoom_ <= 1.01) centre_ = {0.5, 0.5};
  return i;
}

int Framer::preset() const {
  std::lock_guard l(mu_);
  int best = 0;
  for (int k = 1; k < kPresetCount; ++k)
    if (std::abs(kPresets[k] - zoom_) < std::abs(kPresets[best] - zoom_)) best = k;
  return best;
}

void Framer::setPreset(int i) {
  std::lock_guard l(mu_);
  zoom_ = kPresets[std::clamp(i, 0, kPresetCount - 1)];
  if (zoom_ <= 1.01) centre_ = {0.5, 0.5};
}

void Framer::setFollow(bool on) {
  std::lock_guard l(mu_);
  follow_ = on;
  if (!on) followSet_ = false;
}

bool Framer::followOn() const {
  std::lock_guard l(mu_);
  return follow_;
}

void Framer::setFollowSpan(double s) {
  std::lock_guard l(mu_);
  span_ = std::clamp(s, kFollowSpanMin, kFollowSpanMax);
}

double Framer::followSpan() const {
  std::lock_guard l(mu_);
  return span_;
}

bool Framer::takeSpanChanged() {
  std::lock_guard l(mu_);
  return std::exchange(spanChanged_, false);
}

void Framer::beginChord() {
  std::lock_guard l(mu_);
  chord_ = true;
  chordAdjusted_ = false;
}

bool Framer::endChord() {
  std::lock_guard l(mu_);
  if (!chord_) return follow_;
  chord_ = false;
  follow_ = chordAdjusted_ ? true : !follow_;   // setting how close never turns it off
  if (!follow_) followSet_ = false;
  return follow_;
}

void Framer::reset() {
  std::lock_guard l(mu_);
  zoom_ = 1;
  centre_ = {0.5, 0.5};
  follow_ = chord_ = false;
  followSet_ = holdSet_ = boxing_ = false;
  grab_ = {};
  pinch_[0] = pinch_[1] = -1;
  holdSince_ = -1;
  target_ = QRectF(0, 0, 1, 1);   // at once, even before the next picture
}

QRectF Framer::aspectBox(QPointF c, double h) const {
  // Fractions of a picture whose own shape is the output's, so width = height in output terms.
  h = std::clamp(h, 0.1, 1.0);
  const double w = h;
  QRectF r(c.x() - w / 2, c.y() - h / 2, w, h);
  r.moveLeft(std::clamp(r.left(), 0.0, 1.0 - w));
  r.moveTop(std::clamp(r.top(), 0.0, 1.0 - h));
  return r;
}

void Framer::update(const vision::Observation &obs, double now) {
  std::lock_guard l(mu_);
  const double W = std::max(1, obs.width), H = std::max(1, obs.height);
  auto frac = [&](Vec2 p) { return QPointF(p.x / W, p.y / H); };
  marks_.clear();
  const bool following = follow_ || chord_;

  // Follow face first: the pinch below may set how close it is.
  if (obs.face) {
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
    for (const auto &p : obs.face->lm) {
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    }
    faceH_ = std::max(0.02, (y1 - y0) / H);
    if (following) {
      const QPointF c((x0 + x1) / 2 / W, (y0 + y1) / 2 / H + faceH_ * 0.1);
      followCrop_ = aspectBox(c, faceH_ * span_);
      followSet_ = true;
      faceSeen_ = now;
    }
  } else if (followSet_ && faceSeen_ >= 0 && now - faceSeen_ > kFaceLost) {
    followSet_ = false;   // eases back
  }

  // Pinch box: both hands pinching, started within half a second of each other.
  for (int i = 0; i < 2; ++i) {
    const bool p = i < int(obs.hands.size()) && vision::isPinch(obs.hands[size_t(i)]);
    if (!p) pinch_[i] = -1;
    else if (pinch_[i] < 0) pinch_[i] = now;
  }
  const bool twoPinch = moves_.box && obs.hands.size() == 2 && pinch_[0] >= 0 && pinch_[1] >= 0 &&
                        std::abs(pinch_[0] - pinch_[1]) <= kBoxWindow;
  if (twoPinch) {
    const Vec2 pa = pinchPoint(obs.hands[0]), pb = pinchPoint(obs.hands[1]);
    const QPointF a = frac(pa), b = frac(pb);
    const double h = std::max(std::abs(a.y() - b.y()), std::abs(a.x() - b.x()));
    boxCrop_ = aspectBox((a + b) / 2, h * 1.1);
    boxing_ = true;
    grab_ = {};
    holdSince_ = -1;
    marks_.push_back({(pa + pb) / 2, 1.f, QString("Box %1x").arg(1.0 / boxCrop_.height(), 0, 'f', 1)});
  } else if (boxing_) {
    boxing_ = false;   // let go: the box stays as the zoom
    zoom_ = std::clamp(1.0 / boxCrop_.height(), 1.0, kMaxZoom);
    centre_ = boxCrop_.center();
  }

  // A pinch sets how close follow is only while follow frames a face, or its gesture is held: with
  // no face in sight the picture is the manual zoom, and the pinch must move that.
  const bool adjustFollow = chord_ || (follow_ && followSet_);

  // One hand pinching: the pinch the grab is following, else the first one.
  const vision::Hand *ph = nullptr;
  if (!boxing_) {
    double best = 1e18;
    for (const auto &h : obs.hands) {
      // A held grab is looser (the tips drift apart while the hand moves) but only for the same hand.
      const bool same = grab_.held && span(pinchPoint(h), grab_.at) < handSize(h) * 2 &&
                        span(h.lm[4].xy(), h.lm[8].xy()) < span(h.lm[0].xy(), h.lm[9].xy()) * 0.4;
      if (!same && !vision::isPinch(h)) continue;
      const double d = grab_.arming || grab_.held ? span(pinchPoint(h), grab_.at) : 0;
      if (d < best) {
        best = d;
        ph = &h;
      }
    }
  }

  if (moves_.pinchZoom && !boxing_) {
    if (ph) {
      const Vec2 at = pinchPoint(*ph);
      const double size = handSize(*ph);
      grab_.lostAt = -1;
      grab_.at = at;
      if (!grab_.arming && !grab_.held) {
        grab_.arming = true;
        grab_.since = now;
        grab_.at0 = at;
        grab_.size = size;
      }
      grab_.size = grab_.size * (1 - kSizeSmooth) + size * kSizeSmooth;
      if (grab_.arming) {
        // No holding still: the hand may already be moving (pinch, pull, let go, again).
        const float ring = float(std::clamp((now - grab_.since) / kGrab, 0.0, 1.0));
        if (ring >= 1) {
          grab_.arming = false;
          grab_.held = true;
          grab_.size0 = grab_.size;
          grab_.forFollow = adjustFollow;
          grab_.base = adjustFollow ? span_ : zoom_;
        } else {
          marks_.push_back({at, ring, adjustFollow ? "Follow: how close" : "Pinch zoom"});
        }
      }
      if (grab_.held) {
        if (grab_.forFollow != adjustFollow) {   // follow began or ended framing mid-grab
          grab_.forFollow = adjustFollow;
          grab_.size0 = grab_.size;
          grab_.base = adjustFollow ? span_ : zoom_;
        }
        // The size change past a small deadband, so a steady pinch doesn't creep.
        double r = std::log(grab_.size / std::max(1e-6, grab_.size0));
        r = r > 0 ? std::max(0.0, r - std::log1p(kDeadband)) : std::min(0.0, r + std::log1p(kDeadband));
        const double f = std::exp(r * kGain);
        if (adjustFollow) {
          span_ = std::clamp(grab_.base / f, kFollowSpanMin, kFollowSpanMax);
          chordAdjusted_ |= chord_;
          spanChanged_ = true;
          marks_.push_back({at, 1.f, QString("Follow %1x").arg(std::max(1.0, 1.0 / (faceH_ * span_)), 0, 'f', 1)});
        } else {
          zoom_ = std::clamp(grab_.base * f, 1.0, kMaxZoom);
          if (zoom_ <= 1.01) centre_ = {0.5, 0.5};
          marks_.push_back({at, 1.f, QString("Zoom %1x").arg(zoom_, 0, 'f', 1)});
        }
      }
    } else if (grab_.arming || grab_.held) {
      if (grab_.lostAt < 0) grab_.lostAt = now;
      if (now - grab_.lostAt > kLetGo) grab_ = {};   // let go: the zoom stays
    }
  } else if (moves_.hold && !boxing_) {
    // Zoom on what I hold: one pinch held still; the picture follows it until the pinch lets go.
    if (!ph) {
      holdSince_ = -1;
      holdSet_ = false;
    } else {
      const Vec2 at = pinchPoint(*ph);
      const double size = handSize(*ph);
      if (holdSince_ < 0 || (!holdSet_ && span(at, holdAt_) > kStill * size)) {
        holdSince_ = now;
        holdAt_ = at;
      }
      if (now - holdSince_ >= kHoldPinch || holdSet_) {
        holdCrop_ = aspectBox(frac(ph->lm[8].xy()), size / H * 3.2);
        holdSet_ = true;
      } else {
        marks_.push_back({at, float((now - holdSince_) / kHoldPinch), "Zoom on what I hold"});
      }
    }
  }

  // Priority: what the hands are doing now, then follow, then the manual zoom.
  if (boxing_) target_ = boxCrop_;
  else if (holdSet_) target_ = holdCrop_;
  else if ((follow_ || chord_) && followSet_) target_ = followCrop_;
  else target_ = aspectBox(centre_, 1.0 / zoom_);
}

std::vector<Framer::Mark> Framer::marks() const {
  std::lock_guard l(mu_);
  return marks_;
}

QRectF Framer::target() const {
  std::lock_guard l(mu_);
  return target_;
}

QRectF Framer::crop(double now) {
  std::lock_guard l(mu_);
  if (curAt_ < 0) {
    cur_ = target_;
  } else {
    const double dt = std::clamp(now - curAt_, 0.0, 0.5);
    const double k = 1 - std::exp(-dt / kEase);
    cur_ = QRectF(cur_.x() + (target_.x() - cur_.x()) * k, cur_.y() + (target_.y() - cur_.y()) * k,
                  cur_.width() + (target_.width() - cur_.width()) * k, cur_.height() + (target_.height() - cur_.height()) * k);
  }
  curAt_ = now;
  return cur_;
}

}  // namespace racg::framing
