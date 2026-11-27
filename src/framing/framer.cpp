#include "framing/framer.hpp"

#include <algorithm>
#include <cmath>

namespace racg::framing {

namespace {
constexpr double kEase = 0.25;   // seconds: the crop covers ~63 % of the way in this time
constexpr int kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0]));

double span(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
}  // namespace

bool pinching(const vision::Hand &h) {
  const double size = span(h.lm[0].xy(), h.lm[9].xy());   // wrist to middle knuckle
  return size > 1 && span(h.lm[4].xy(), h.lm[8].xy()) < size * 0.25;
}

void Framer::setAspect(float a) {
  std::lock_guard l(mu_);
  if (a > 0.1f) aspect_ = a;
}

int Framer::stepPreset(int delta) {
  std::lock_guard l(mu_);
  preset_ = std::clamp(preset_ + delta, 0, kPresetCount - 1);
  return preset_;
}

int Framer::preset() const {
  std::lock_guard l(mu_);
  return preset_;
}

void Framer::setPreset(int i) {
  std::lock_guard l(mu_);
  preset_ = std::clamp(i, 0, kPresetCount - 1);
}

void Framer::setBox(bool on) {
  std::lock_guard l(mu_);
  box_ = on;
  boxSet_ = false;
  pinch_[0] = pinch_[1] = -1;
}

void Framer::setHold(bool on) {
  std::lock_guard l(mu_);
  hold_ = on;
  holdSet_ = false;
  holdSince_ = -1;
}

void Framer::setFollow(bool on) {
  std::lock_guard l(mu_);
  follow_ = on;
  followSet_ = false;
}

bool Framer::boxOn() const {
  std::lock_guard l(mu_);
  return box_;
}
bool Framer::holdOn() const {
  std::lock_guard l(mu_);
  return hold_;
}
bool Framer::followOn() const {
  std::lock_guard l(mu_);
  return follow_;
}

void Framer::reset() {
  std::lock_guard l(mu_);
  preset_ = 0;
  box_ = hold_ = follow_ = false;
  boxSet_ = holdSet_ = followSet_ = false;
  target_ = {0, 0, 1, 1};
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

  // Two pinches within half a second: the box between the pinch points, with a little margin.
  if (box_) {
    for (int i = 0; i < 2; ++i) {
      const bool p = i < int(obs.hands.size()) && pinching(obs.hands[size_t(i)]);
      if (!p) pinch_[i] = -1;
      else if (pinch_[i] < 0) pinch_[i] = now;
    }
    if (obs.hands.size() == 2 && pinch_[0] >= 0 && pinch_[1] >= 0 && std::abs(pinch_[0] - pinch_[1]) <= kBoxWindow) {
      const QPointF a = frac((obs.hands[0].lm[4].xy() + obs.hands[0].lm[8].xy()) / 2);
      const QPointF b = frac((obs.hands[1].lm[4].xy() + obs.hands[1].lm[8].xy()) / 2);
      const double h = std::max(std::abs(a.y() - b.y()), std::abs(a.x() - b.x()));
      boxCrop_ = aspectBox((a + b) / 2, h * 1.1);
      boxSet_ = true;
    }
  }
  // One pinch held: the hand and what it holds, about three hand-sizes tall, following the hand.
  if (hold_) {
    const vision::Hand *ph = nullptr;
    for (const auto &h : obs.hands)
      if (pinching(h)) ph = &h;
    if (!ph) {
      holdSince_ = -1;
    } else {
      if (holdSince_ < 0) holdSince_ = now;
      if (now - holdSince_ >= kHoldPinch || holdSet_) {
        const double size = span(ph->lm[0].xy(), ph->lm[9].xy()) / H;
        holdCrop_ = aspectBox(frac(ph->lm[8].xy()), size * 3.2);
        holdSet_ = true;
      }
    }
  }
  // Follow face: shoulders up (about three face heights), the face a little above the middle.
  if (follow_) {
    if (obs.face) {
      float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
      for (const auto &p : obs.face->lm) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
      }
      const double fh = (y1 - y0) / H;
      const QPointF c((x0 + x1) / 2 / W, (y0 + y1) / 2 / H + fh * 0.45);
      followCrop_ = aspectBox(c, fh * 3.2);
      followSet_ = true;
      faceSeen_ = now;
    } else if (faceSeen_ >= 0 && now - faceSeen_ > kFaceLost) {
      followSet_ = false;   // eases back
    }
  }

  // Priority: what the hands asked for, then follow, then the preset.
  if (box_ && boxSet_) target_ = boxCrop_;
  else if (hold_ && holdSet_) target_ = holdCrop_;
  else if (follow_ && followSet_) {
    const double z = kPresets[preset_];
    target_ = z > 1 ? aspectBox(followCrop_.center(), std::min(followCrop_.height(), 1.0 / z)) : followCrop_;
  } else {
    const double z = kPresets[preset_];
    target_ = aspectBox({0.5, 0.5}, 1.0 / z);
  }
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
