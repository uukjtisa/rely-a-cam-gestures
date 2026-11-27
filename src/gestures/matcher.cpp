#include "gestures/matcher.hpp"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace racg::gestures {

namespace {

constexpr float kZCapFace = 8.f;
constexpr float kTrue = 100.f / 60.f;

// Hand shape: cosine 0.65 is the default line, 0.95 is 100 %.
float handPct(float cosine) { return std::clamp(60.f + (cosine - 0.65f) * (40.f / 0.30f), 0.f, 100.f); }
// Where the palm is next to the face: the same place keeps it, a face width off costs a quarter.
float placeFactor(Vec2 a, Vec2 b) {
  const float d = dist(a, b);
  return 0.6f + 0.4f * std::exp(-d * d / (2 * 0.7f * 0.7f));
}

bool isGaze(int i) { return std::strncmp(vision::blendshapeName(i), "eyeLook", 7) == 0; }

QJsonArray arr(const float *v, size_t n) {
  QJsonArray a;
  for (size_t i = 0; i < n; ++i) a.append(double(std::round(v[i] * 1e5) / 1e5));
  return a;
}
std::vector<float> vec(const QJsonValue &v) {
  std::vector<float> out;
  for (const auto &x : v.toArray()) out.push_back(float(x.toDouble()));
  return out;
}

float cosine(const std::array<float, 128> &a, const std::array<float, 128> &b) {
  float d = 0, na = 0, nb = 0;
  for (int i = 0; i < 128; ++i) {
    d += a[i] * b[i];
    na += a[i] * a[i];
    nb += b[i] * b[i];
  }
  return d / std::sqrt(na * nb + 1e-12f);
}

const vision::Hand *handOf(const vision::Observation &obs, int side, unsigned *bit) {
  for (size_t i = 0; i < obs.hands.size(); ++i)
    if (obs.hands[i].isLeft() == (side == 0)) {
      *bit = 1u << i;
      return &obs.hands[i];
    }
  return nullptr;
}

Vec2 placeOf(const vision::Hand &h, const FaceGeom &f) {
  const HandGeom g = handGeom(h);
  return (g.palm - f.nose) / std::max(f.w, 1.f);
}

}  // namespace

QJsonObject Parts::toJson() const {
  return {{"left_hand", leftHand}, {"right_hand", rightHand}, {"face", face}, {"body", body}};
}
Parts Parts::fromJson(const QJsonObject &o) {
  return {o["left_hand"].toBool(), o["right_hand"].toBool(), o["face"].toBool(), o["body"].toBool()};
}

QJsonObject Take::toJson() const {
  QJsonObject o;
  const char *names[2] = {"left", "right"};
  for (int s = 0; s < 2; ++s) {
    if (!hand[s].seen) continue;
    QJsonObject h{{"embedding", arr(hand[s].embedding.data(), 128)}};
    if (hand[s].placed) h["place"] = QJsonArray{hand[s].place.x, hand[s].place.y};
    o[names[s]] = h;
  }
  if (!face.empty()) o["face"] = arr(face.data(), face.size());
  if (!arms.empty()) o["arms"] = arr(arms.data(), arms.size());
  if (!thumb.isEmpty()) o["thumb"] = thumb;
  return o;
}

Take Take::fromJson(const QJsonObject &o) {
  Take t;
  const char *names[2] = {"left", "right"};
  for (int s = 0; s < 2; ++s) {
    const QJsonObject h = o[names[s]].toObject();
    const auto e = vec(h["embedding"]);
    if (e.size() != 128) continue;
    t.hand[s].seen = true;
    std::copy(e.begin(), e.end(), t.hand[s].embedding.begin());
    const QJsonArray p = h["place"].toArray();
    if (p.size() == 2) {
      t.hand[s].placed = true;
      t.hand[s].place = {float(p[0].toDouble()), float(p[1].toDouble())};
    }
  }
  t.face = vec(o["face"]);
  if (t.face.size() != size_t(vision::kBlendshapeCount)) t.face.clear();
  t.arms = vec(o["arms"]);
  t.thumb = o["thumb"].toString();
  return t;
}

std::vector<float> faceZ(const vision::Face &f, const Baseline &base) {
  std::vector<float> z(vision::kBlendshapeCount);
  for (int i = 0; i < vision::kBlendshapeCount; ++i)
    z[i] = std::clamp(base.z(vision::blendshapeName(i), f.blend[i]), -kZCapFace, kZCapFace);
  return z;
}

void TakeBuilder::add(const vision::Observation &obs, const Baseline &base) {
  ++n_;
  const std::optional<FaceGeom> fg = obs.face ? std::optional(faceGeom(*obs.face)) : std::nullopt;
  for (int s = 0; s < 2; ++s) {
    unsigned bit = 0;
    const vision::Hand *h = handOf(obs, s, &bit);
    if (!h) continue;
    // Average unit vectors, so every frame weighs the same.
    float n = 0;
    for (float v : h->embedding) n += v * v;
    n = std::sqrt(n) + 1e-12f;
    for (int i = 0; i < 128; ++i) emb_[s][i] += h->embedding[i] / n;
    ++nHand_[s];
    if (fg) {
      place_[s] += placeOf(*h, *fg);
      ++nPlaced_[s];
    }
  }
  if (obs.face) {
    const auto z = faceZ(*obs.face, base);
    if (face_.empty()) face_.assign(z.size(), 0);
    for (size_t i = 0; i < z.size(); ++i) face_[i] += z[i];
    ++nFace_;
  }
  if (obs.body && obs.body->lm[11].visibility > 0.5f && obs.body->lm[12].visibility > 0.5f) {
    const auto a = armVector(*obs.body);
    if (arms_.empty()) arms_.assign(a.size(), 0);
    for (size_t i = 0; i < a.size(); ++i) arms_[i] += a[i];
    ++nArms_;
  }
  if (n_ == 15 || pic_.isNull()) pic_ = obs.frame;
}

std::optional<Take> TakeBuilder::finish() const {
  Take t;
  bool any = false;
  for (int s = 0; s < 2; ++s) {
    // A hand counts when it was there for most of the hold, not a passing glimpse.
    if (nHand_[s] < std::max(1, n_ / 2)) continue;
    any = true;
    t.hand[s].seen = true;
    float n = 0;
    for (int i = 0; i < 128; ++i) n += float(emb_[s][i] * emb_[s][i]);
    n = std::sqrt(n) + 1e-12f;
    for (int i = 0; i < 128; ++i) t.hand[s].embedding[i] = float(emb_[s][i]) / n;
    if (nPlaced_[s] >= nHand_[s] / 2 && nPlaced_[s] > 0) {
      t.hand[s].placed = true;
      t.hand[s].place = place_[s] / float(nPlaced_[s]);
    }
  }
  if (nFace_ >= std::max(1, n_ / 2)) {
    any = true;
    for (double v : face_) t.face.push_back(float(v / nFace_));
  }
  if (nArms_ >= std::max(1, n_ / 2)) {
    any = true;
    for (double v : arms_) t.arms.push_back(float(v / nArms_));
  }
  if (!any) return std::nullopt;
  return t;
}

Parts defaultParts(const std::vector<Take> &takes, const Baseline &base) {
  Parts p;
  for (const auto &t : takes) {
    p.leftHand |= t.hand[0].seen;
    p.rightHand |= t.hand[1].seen;
    for (size_t i = 0; i < t.face.size(); ++i)
      if (!isGaze(int(i)) && std::abs(t.face[i]) >= 4.f) p.face = true;
    if (!t.arms.empty() && base.bodyNeutral.size() == t.arms.size()) {
      float d = 0;
      for (size_t i = 0; i < t.arms.size(); ++i) d += std::abs(t.arms[i] - base.bodyNeutral[i]);
      if (d / t.arms.size() > 0.5f) p.body = true;
    }
  }
  return p;
}

Match matchCustom(const std::vector<Take> &takes, const Parts &parts, const vision::Observation &obs,
                  const Baseline &base) {
  Match best;
  if (!parts.any()) return best;
  const std::optional<FaceGeom> fg = obs.face ? std::optional(faceGeom(*obs.face)) : std::nullopt;
  const std::vector<float> z = obs.face ? faceZ(*obs.face, base) : std::vector<float>{};
  const std::vector<float> arms =
      obs.body && obs.body->lm[11].visibility > 0.5f ? armVector(*obs.body) : std::vector<float>{};
  for (const auto &t : takes) {
    float score = 1e9f;
    unsigned hands = 0;
    // A take asks only for the hands it has: "both hands" ticked over a left-hand take and a
    // right-hand take means either hand, not both at once (no take holds both).
    const bool want[2] = {parts.leftHand && t.hand[0].seen, parts.rightHand && t.hand[1].seen};
    for (int s = 0; s < 2 && score > 0; ++s) {
      if (!want[s]) continue;
      unsigned bit = 0;
      const vision::Hand *h = handOf(obs, s, &bit);
      if (!h) {
        score = 0;
        break;
      }
      float pct = handPct(cosine(h->embedding, t.hand[s].embedding));
      if (t.hand[s].placed && fg) pct *= placeFactor(placeOf(*h, *fg), t.hand[s].place);
      score = std::min(score, pct / 60.f);
      hands |= bit;
    }
    if (parts.face && score > 0) {
      if (z.empty() || t.face.empty()) {
        score = 0;
      } else {
        // The channels the take moved (gaze aside): how far you are toward the recorded expression.
        float sum = 0;
        int n = 0;
        for (size_t i = 0; i < t.face.size(); ++i) {
          if (isGaze(int(i)) || std::abs(t.face[i]) < 3.f) continue;
          sum += std::clamp(z[i] / t.face[i], 0.f, 1.f);
          ++n;
        }
        float s;
        if (n) {
          s = sum / n;
        } else {   // ticked but nothing moved: stay close to the recorded (neutral) face
          float d = 0;
          for (size_t i = 0; i < t.face.size(); ++i) d += std::abs(z[i] - t.face[i]);
          s = std::clamp(1.f - d / t.face.size() / 6.f, 0.f, 1.f);
        }
        score = std::min(score, s / 0.75f);
      }
    }
    if (parts.body && score > 0) {
      if (arms.empty() || t.arms.size() != arms.size()) {
        score = 0;
      } else {
        float d = 0;
        for (size_t i = 0; i < arms.size(); ++i) d += std::abs(arms[i] - t.arms[i]);
        score = std::min(score, std::clamp(1.f - d / arms.size() / 0.6f, 0.f, 1.f) / 0.7f);
      }
    }
    if (score > 1e8f) score = 0;
    if (score > best.score) best = {score, hands};
  }
  return best;
}

Match matchSign(vision::Sign sign, const vision::Observation &obs) {
  Match best;
  for (size_t i = 0; i < obs.hands.size(); ++i) {
    // A pinching hand is framing (pinch zoom, the box), not signing: it can read as a fist, and
    // setting how close follow face is must not turn the camera off.
    if (vision::isPinch(obs.hands[i])) continue;
    const float s = obs.hands[i].signs[int(sign)] / 0.5f;   // 60 % at Google's own 0.5
    if (s > best.score) best = {s, 1u << i};
  }
  return best;
}

float stopSignScore(StopSign s, const vision::Observation &obs) {
  if (s == StopSign::BothPalms) {
    if (obs.hands.size() < 2) return 0;
    return std::min(obs.hands[0].signs[int(vision::Sign::OpenPalm)], obs.hands[1].signs[int(vision::Sign::OpenPalm)]) /
           0.5f;
  }
  if (!obs.body) return 0;
  const auto &b = obs.body->lm;
  for (int i : {11, 12, 13, 14, 15, 16})
    if (b[i].visibility < 0.5f) return 0;
  // Crossed: the wrists swapped sides relative to the shoulders, both in front of the chest.
  const float sw = std::max(std::abs(b[11].x - b[12].x), 1.f);
  const float side = b[11].x > b[12].x ? 1.f : -1.f;
  const float cross = -(b[15].x - b[16].x) * side / sw;
  const float shoulderY = (b[11].y + b[12].y) / 2;
  const bool chest = b[15].y > shoulderY && b[16].y > shoulderY && b[15].y < shoulderY + 1.5f * sw &&
                     b[16].y < shoulderY + 1.5f * sw;
  return chest && cross > 0.1f ? kTrue : 0.f;
}

}  // namespace racg::gestures
