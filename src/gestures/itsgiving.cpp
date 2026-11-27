#include "gestures/itsgiving.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPolygonF>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace racg::gestures {

using vision::blendshapeIndex;

namespace {

struct Info {
  const char *id, *name, *how;
  int arm;   // ARM, else 3
};
// POSES order, with ARM from its_giving_v2.py.
constexpr Info kInfo[kReactionCount] = {
    {"time_out", "Time out", "both hands make a T", 3},
    {"heart", "Heart hands", "both hands", 3},
    {"cover_nose", "Cover nose", "both hands over nose and mouth", 3},
    {"crashing_out", "Crashing out", "hands on head, mouth open", 4},
    {"dance", "Dance", "hands up behind the head", 6},
    {"nose_closed", "Nose closed", "nose pinched", 3},
    {"flirty", "Flirty", "fingertip on the lips", 3},
    {"hand_up", "Hand up", "open palm beside the head", 3},
    {"tongue_out", "Tongue out", "face", 5},
    {"open_mouth", "Open mouth", "face", 4},
    {"disgusted", "Disgusted", "face", 5},
    {"talking_to_wall", "Talking to the wall", "hands gesturing away", 6},
    {"suspicious", "Suspicious", "head turned, squinting", 8},
    {"spin", "Spin", "leave the frame", 15},
};

// Z, Z_CAP, FLOOR and T from its_giving_v2.py.
constexpr float kZJawOpen = 6.0f, kZScreamJaw = 3.5f, kZTongueJaw = 3.5f, kZSneer = 4.5f, kZDisgust = 14.0f,
                kZSquint = 4.0f, kZCap = 8.0f;
constexpr float kFloorJawOpen = 0.30f, kFloorScreamJaw = 0.18f, kFloorTongueJaw = 0.18f, kFloorSneer = 0.06f,
                kFloorSquint = 0.18f;
constexpr float kTTongue = 0.5f, kTHeadTurn = 0.15f;
constexpr float kTGesture = 0.035f * 30;   // 0.035 face widths per frame at 30 fps, per second

constexpr float kSigmaFloor = 0.015f, kSigmaCeil = 0.080f, kGenericSigma = 0.035f;
constexpr int kCalibVersion = 1;

constexpr float kCap = 10.f;            // ratios are capped so a zero distance stays finite
constexpr float kTrue = 100.f / 60.f;   // a rule with nothing to soften: 100 % when it holds

float genericMean(const std::string &n) {
  static const std::map<std::string, float> m = {
      {"jawOpen", 0.08f}, {"eyeSquintLeft", 0.10f}, {"eyeSquintRight", 0.10f}, {"eyeBlinkLeft", 0.10f},
      {"eyeBlinkRight", 0.10f}, {"noseSneerLeft", 0.03f}, {"noseSneerRight", 0.03f}, {"browDownLeft", 0.06f},
      {"browDownRight", 0.06f}, {"mouthFrownLeft", 0.05f}, {"mouthFrownRight", 0.05f},
      {"mouthUpperUpLeft", 0.05f}, {"mouthUpperUpRight", 0.05f}};
  auto it = m.find(n);
  return it == m.end() ? 0.02f : it->second;
}

float lt(float a, float k) { return a <= 0 ? kCap : std::min(kCap, k / a); }   // a < k
float gt(float a, float k) { return std::min(kCap, a / k); }                    // a > k
float above(float margin, float fw) { return std::min(kCap, 1 + margin / fw); } // margin > 0
float min3(float a, float b, float c) { return std::min(a, std::min(b, c)); }

float round5(double v) { return float(std::round(v * 1e5) / 1e5); }

}  // namespace

const char *reactionId(Reaction r) { return kInfo[int(r)].id; }
const char *reactionName(Reaction r) { return kInfo[int(r)].name; }
const char *reactionHow(Reaction r) { return kInfo[int(r)].how; }
float reactionArmSeconds(Reaction r) { return kInfo[int(r)].arm / 30.f; }

// ---- geometry ------------------------------------------------------------------------------------

FaceGeom faceGeom(const vision::Face &f) {
  FaceGeom g;
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (const auto &l : f.lm) {
    x0 = std::min(x0, l.x); y0 = std::min(y0, l.y); x1 = std::max(x1, l.x); y1 = std::max(y1, l.y);
  }
  g.box = {x0, y0, x1 - x0, y1 - y0};
  g.w = x1 - x0;
  g.h = y1 - y0;
  g.center = {(x0 + x1) / 2, (y0 + y1) / 2};
  g.nose = f.lm[1].xy();
  g.chin = f.lm[152].xy();
  g.top = f.lm[10].xy();
  g.mouth = (f.lm[13].xy() + f.lm[14].xy()) / 2;
  g.eyeY = (f.lm[33].y + f.lm[263].y) / 2;
  const Vec2 cl = f.lm[234].xy(), cr = f.lm[454].xy();
  g.turnSigned = (g.nose.x - cl.x) / std::max(cr.x - cl.x, 1e-3f) - 0.5f;
  return g;
}

HandGeom handGeom(const vision::Hand &h) {
  HandGeom g;
  const auto p = [&](int i) { return h.lm[i].xy(); };
  g.palm = (p(0) + p(5) + p(9) + p(13) + p(17)) / 5;
  g.thumb = p(4);
  g.index = p(8);
  g.middle = p(12);
  const Vec2 d = p(9) - p(0);
  g.horizontal = gt(std::abs(d.x), 1.5f * std::max(std::abs(d.y), 1e-3f));
  g.vertical = gt(std::abs(d.y), 1.5f * std::max(std::abs(d.x), 1e-3f));
  // ext = dist(p0, tip) > 1.2 dist(p0, tip - 2) per finger; open = at least three of four.
  std::array<float, 4> ext;
  int k = 0;
  for (int t : {8, 12, 16, 20}) ext[k++] = gt(dist(p(0), p(t)), 1.2f * std::max(dist(p(0), p(t - 2)), 1e-3f));
  std::sort(ext.begin(), ext.end(), std::greater<>());
  g.open = ext[2];
  return g;
}

BodyGeom bodyGeom(const vision::Body &b, float faceWidth) {
  BodyGeom g;
  for (int i = 0; i < 2; ++i) {
    g.shoulders[i] = b.lm[11 + i].xy();
    g.elbows[i] = b.lm[13 + i].xy();
    g.wrists[i] = b.lm[15 + i].xy();
  }
  float vis = 1;
  for (int i : {11, 12, 13, 14}) vis = std::min(vis, b.lm[i].visibility);
  g.seen = gt(vis, 0.5f);
  const float shoulderY = (g.shoulders[0].y + g.shoulders[1].y) / 2;
  const float fw = std::max(faceWidth, 1.f);
  g.elbowsUp = min3(g.seen, above(shoulderY - g.elbows[0].y, fw), above(shoulderY - g.elbows[1].y, fw));
  return g;
}

std::vector<float> armVector(const vision::Body &b) {
  const Vec2 ls = b.lm[11].xy(), rs = b.lm[12].xy();
  const float sw = std::max(dist(ls, rs), 1.f);
  std::vector<float> v;
  for (int side = 0; side < 2; ++side) {
    const Vec2 s = b.lm[11 + side].xy(), e = b.lm[13 + side].xy(), w = b.lm[15 + side].xy();
    for (Vec2 d : {e - s, w - e}) {
      v.push_back(d.x / sw);
      v.push_back(d.y / sw);
    }
  }
  return v;
}

// ---- baseline ------------------------------------------------------------------------------------

float Baseline::z(const std::string &name, float value) const {
  if (generic()) return (value - genericMean(name)) / kGenericSigma;
  auto m = mean.find(name);
  if (m == mean.end()) return (value - genericMean(name)) / kGenericSigma;
  auto s = sigma.find(name);
  return (value - m->second) / (s == sigma.end() ? kSigmaCeil : s->second);
}

float Baseline::neutralTurn() const {
  if (generic()) return 0;
  auto it = mean.find("turn_signed");
  return it == mean.end() ? 0 : it->second;
}

QJsonObject Baseline::toJson() const {
  QJsonObject m, s;
  for (const auto &[k, v] : mean) m[QString::fromStdString(k)] = v;
  for (const auto &[k, v] : sigma) s[QString::fromStdString(k)] = v;
  QJsonObject o{{"version", kCalibVersion}, {"made", made}, {"samples", samples}, {"mean", m}, {"sigma", s}};
  if (!bodyNeutral.empty()) {
    QJsonArray b;
    for (float v : bodyNeutral) b.append(v);
    o["body_neutral"] = b;   // ours; itsgiving ignores keys it doesn't know
  }
  return o;
}

Baseline Baseline::fromJson(const QJsonObject &o) {
  Baseline b;
  if (o["version"].toInt() != kCalibVersion || o["mean"].toObject().isEmpty()) return b;
  const QJsonObject m = o["mean"].toObject(), s = o["sigma"].toObject();
  for (auto it = m.begin(); it != m.end(); ++it) b.mean[it.key().toStdString()] = float(it.value().toDouble());
  for (auto it = s.begin(); it != s.end(); ++it) b.sigma[it.key().toStdString()] = float(it.value().toDouble());
  b.samples = o["samples"].toInt();
  b.made = o["made"].toString();
  for (const auto &v : o["body_neutral"].toArray()) b.bodyNeutral.push_back(float(v.toDouble()));
  return b;
}

bool Baseline::save(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
  return f.commit();
}

Baseline Baseline::load(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return {};
  return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

void Collector::add(const vision::Face &face, const vision::Body *body) {
  ++n_;
  auto put = [&](const std::string &k, double v) {
    s_[k] += v;
    ss_[k] += v * v;
  };
  for (int i = 0; i < vision::kBlendshapeCount; ++i) put(vision::blendshapeName(i), face.blend[i]);
  put("turn_signed", faceGeom(face).turnSigned);
  if (body && body->lm[11].visibility > 0.5f && body->lm[12].visibility > 0.5f) {
    const auto v = armVector(*body);
    if (body_.empty()) body_.assign(v.size(), 0);
    for (size_t i = 0; i < v.size(); ++i) body_[i] += v[i];
    ++bodyN_;
  }
}

Baseline Collector::finish(const QString &made) const {
  Baseline b;
  for (const auto &[name, total] : s_) {
    const double m = total / n_;
    const double var = std::max(ss_.at(name) / n_ - m * m, 0.0);
    b.mean[name] = round5(m);
    b.sigma[name] = round5(std::min(std::max(std::sqrt(var), double(kSigmaFloor)), double(kSigmaCeil)));
  }
  auto t = b.sigma.find("turn_signed");
  b.sigma["turn_signed"] = std::min(std::max(t == b.sigma.end() ? 0.02f : t->second, 0.01f), 0.10f);
  b.samples = n_;
  b.made = made;
  if (bodyN_ > n_ / 2)
    for (double v : body_) b.bodyNeutral.push_back(float(v / bodyN_));
  return b;
}

std::vector<QString> calibrationWarnings(const Baseline &b) {
  std::vector<QString> out;
  auto m = [&](const char *k) { auto it = b.mean.find(k); return it == b.mean.end() ? 0.f : it->second; };
  if (m("jawOpen") > 0.30f) out.push_back("Your mouth looked open. Don't talk while it calibrates.");
  if (std::max(m("noseSneerLeft"), m("noseSneerRight")) > 0.15f)
    out.push_back("Your nose was scrunched. Hold a bored face, not a reaction.");
  if (std::max(m("browInnerUp"), m("browOuterUpLeft")) > 0.35f) out.push_back("Your eyebrows were up. Relax them.");
  int pinned = 0;
  for (const auto &[k, v] : b.sigma) pinned += v >= kSigmaCeil;
  if (pinned > 12) out.push_back("You moved a lot. Sit still and try again for a tighter baseline.");
  return out;
}

// ---- measures ------------------------------------------------------------------------------------

Measures measure(const FaceGeom &g, const vision::Face &f, const Baseline &b) {
  auto raw = [&](const std::string &n) {
    const int i = blendshapeIndex(n);
    return i < 0 ? 0.f : f.blend[i];
  };
  auto zpair = [&](const std::string &n) {
    return (b.z(n + "Left", raw(n + "Left")) + b.z(n + "Right", raw(n + "Right"))) / 2;
  };
  auto pair = [&](const std::string &n) { return (raw(n + "Left") + raw(n + "Right")) / 2; };
  Measures m;
  m.jaw = raw("jawOpen");
  m.zJaw = b.z("jawOpen", m.jaw);
  m.sneer = pair("noseSneer");
  m.zSneer = zpair("noseSneer");
  m.zBrow = zpair("browDown");
  m.zFrown = zpair("mouthFrown");
  m.zLip = zpair("mouthUpperUp");
  m.squint = std::max(pair("eyeSquint"), pair("eyeBlink"));
  m.zSquint = std::max(zpair("eyeSquint"), zpair("eyeBlink"));
  m.turn = std::abs(g.turnSigned - b.neutralTurn());
  auto cap = [](float v) { return std::min(v, kZCap); };
  m.zDisgust = 2 * cap(m.zSneer) + cap(m.zBrow) + cap(m.zFrown) + cap(m.zLip);
  return m;
}

// ---- tongue pixels -------------------------------------------------------------------------------

float mouthPink(const QImage &full, const std::array<Vec2, 20> &lips) {
  // poly.astype(np.int32): truncation toward zero.
  QPolygonF poly;
  int x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
  for (Vec2 p : lips) {
    const int x = int(p.x), y = int(p.y);
    poly << QPointF(x, y);
    x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y);
  }
  if (x1 - x0 < 8 || y1 - y0 < 8) return 0;
  const int k = std::max(3, int(0.15 * (y1 - y0)));
  x0 = std::max(x0, 0);
  y0 = std::max(y0, 0);
  x1 = std::min(x1, full.width() - 1);
  y1 = std::min(y1, full.height() - 1);
  if (x1 < x0 || y1 < y0) return 0;
  const int w = x1 - x0 + 1, h = y1 - y0 + 1;
  // fillPoly, then erode with a k x k square (OpenCV's anchor k/2; outside the ROI never erodes).
  std::vector<uint8_t> mask(size_t(w) * h), tmp(size_t(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) mask[size_t(y) * w + x] = poly.containsPoint(QPointF(x0 + x, y0 + y), Qt::OddEvenFill);
  const int a = k / 2, b = k - 1 - a;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      uint8_t v = 1;
      for (int dx = -a; dx <= b && v; ++dx) {
        const int xx = x + dx;
        if (xx >= 0 && xx < w) v = mask[size_t(y) * w + xx];
      }
      tmp[size_t(y) * w + x] = v;
    }
  int n = 0, pink = 0;
  for (int y = 0; y < h; ++y) {
    const QRgb *row = reinterpret_cast<const QRgb *>(full.constScanLine(y0 + y));
    for (int x = 0; x < w; ++x) {
      uint8_t v = 1;
      for (int dy = -a; dy <= b && v; ++dy) {
        const int yy = y + dy;
        if (yy >= 0 && yy < h) v = tmp[size_t(yy) * w + x];
      }
      if (!v) continue;
      ++n;
      // cv2.COLOR_BGR2HSV, 8-bit: H in [0, 180), S and V in [0, 255].
      const QRgb c = row[x0 + x];
      const int r = qRed(c), g = qGreen(c), bl = qBlue(c);
      const int mx = std::max(r, std::max(g, bl)), mn = std::min(r, std::min(g, bl));
      const float s = mx ? 255.f * (mx - mn) / mx : 0;
      float hue = 0;
      if (mx != mn) {
        if (mx == r) hue = 60.f * (g - bl) / (mx - mn);
        else if (mx == g) hue = 120.f + 60.f * (bl - r) / (mx - mn);
        else hue = 240.f + 60.f * (r - g) / (mx - mn);
        if (hue < 0) hue += 360;
      }
      const float h8 = hue / 2;
      if ((h8 < 12 || h8 > 160) && s > 70 && mx > 110) ++pink;
    }
  }
  return n < 40 ? 0.f : float(pink) / n;
}

// ---- motion --------------------------------------------------------------------------------------

float Motion::update(const std::vector<HandGeom> &hands, const FaceGeom *face, float dt) {
  if (face) fw_ = std::max(face->w, 1.f);
  dt = std::max(dt, 1e-3f);
  float speed = 0;
  if (!hands.empty() && !prev_.empty()) {
    const float jump = fw_ * std::max(1.f, dt * 30);   // itsgiving drops moves over a face width a frame
    for (const auto &h : hands) {
      float m = 1e9f;
      for (Vec2 p : prev_) m = std::min(m, dist(h.palm, p));
      if (m < jump) speed = std::max(speed, m / fw_ / dt);
    }
  }
  const float decay = std::pow(0.8f, dt * 30);
  energy_ = decay * energy_ + (1 - decay) * speed;
  prev_.clear();
  for (const auto &h : hands) prev_.push_back(h.palm);
  return energy_;
}

// ---- decide --------------------------------------------------------------------------------------

ReadyEval evaluate(const vision::Observation &obs, const Baseline &base, Motion &motion, float dt) {
  ReadyEval e;
  std::vector<HandGeom> hands;
  for (const auto &h : obs.hands) hands.push_back(handGeom(h));
  const std::optional<FaceGeom> face = obs.face ? std::optional(faceGeom(*obs.face)) : std::nullopt;
  e.gesture = motion.update(hands, face ? &*face : nullptr, dt);
  auto add = [&](Reaction r, float s, unsigned handsMask) {
    e.order.push_back({r, s, handsMask});
    e.score[int(r)] = std::max(e.score[int(r)], s);
  };
  const unsigned allHands = (1u << hands.size()) - 1;

  if (!face) {
    const bool gone = hands.empty() && (!obs.body || bodyGeom(*obs.body, 1).seen < 1);
    add(Reaction::Spin, gone ? kTrue : 0.f, 0);
    return e;
  }
  const FaceGeom &f = *face;
  const float fw = std::max(f.w, 1.f);
  e.m = measure(f, *obs.face, base);
  const Measures &m = e.m;
  auto near = [&](Vec2 a, Vec2 b, float k) { return lt(dist(a, b), k * fw); };
  const float elbowsUp = obs.body ? bodyGeom(*obs.body, fw).elbowsUp : 0.f;
  const float screaming = std::min(gt(m.zJaw, kZScreamJaw), gt(m.jaw, kFloorScreamJaw));

  // The tongue: only once the jaw is open enough and no hand is at the mouth (tongue_score's gates).
  float tongue = std::min(gt(m.zJaw, kZTongueJaw), gt(m.jaw, kFloorTongueJaw));
  for (const auto &h : hands) tongue = std::min(tongue, gt(dist(h.palm, f.mouth), 0.7f * fw));
  e.tongue = obs.mouthPink;
  tongue = std::min(tongue, gt(obs.mouthPink, kTTongue));

  if (hands.size() >= 2) {
    const HandGeom &a = hands[0], &b = hands[1];
    float t = 0;
    for (auto [top, under] : {std::pair{&a, &b}, std::pair{&b, &a}})
      t = std::max(t, std::min(std::min(top->horizontal, under->vertical),
                               std::min(above(under->palm.y - top->palm.y, fw), near(under->middle, top->palm, 0.6f))));
    add(Reaction::TimeOut, t, 3);
    add(Reaction::Heart,
        min3(near(a.index, b.index, 0.3f), near(a.thumb, b.thumb, 0.3f),
             above((a.thumb.y + b.thumb.y) - (a.index.y + b.index.y), fw)),
        3);
    add(Reaction::CoverNose, std::min(near(a.palm, f.mouth, 0.6f), near(b.palm, f.mouth, 0.6f)), 3);
    auto onHead = [&](const HandGeom &h) {
      return min3(above(f.eyeY - h.palm.y, fw), lt(std::abs(h.palm.x - f.nose.x), 1.1f * fw),
                  above(h.palm.y - (f.top.y - 0.8f * f.h), fw));
    };
    add(Reaction::CrashingOut, min3(onHead(a), onHead(b), screaming), 3);
  }

  float headUp = elbowsUp;
  for (const auto &h : hands)
    headUp = min3(headUp, lt(std::abs(h.palm.x - f.nose.x), 1.3f * fw), above(f.eyeY + 0.3f * f.h - h.palm.y, fw));
  add(Reaction::CrashingOut, std::min(headUp, screaming), allHands);
  add(Reaction::Dance, headUp, allHands);

  for (size_t i = 0; i < hands.size(); ++i) {
    const HandGeom &h = hands[i];
    add(Reaction::NoseClosed, min3(near(h.thumb, f.nose, 0.35f), near(h.index, f.nose, 0.35f), near(h.thumb, h.index, 0.3f)),
        1u << i);
    add(Reaction::Flirty, std::min(near(h.index, f.mouth, 0.22f), gt(dist(h.palm, f.mouth), 0.3f * fw)), 1u << i);
    add(Reaction::HandUp, min3(h.open, above(f.nose.y - h.palm.y, fw), gt(std::abs(h.palm.x - f.nose.x), 0.8f * fw)),
        1u << i);
  }

  add(Reaction::TongueOut, tongue, 0);
  add(Reaction::OpenMouth, std::min(gt(m.zJaw, kZJawOpen), gt(m.jaw, kFloorJawOpen)), 0);
  add(Reaction::Disgusted, std::max(std::min(gt(m.zSneer, kZSneer), gt(m.sneer, kFloorSneer)), gt(m.zDisgust, kZDisgust)), 0);
  add(Reaction::TalkingToWall, hands.empty() ? 0.f : gt(e.gesture, kTGesture), allHands);
  add(Reaction::Suspicious, min3(gt(m.turn, kTHeadTurn), gt(m.zSquint, kZSquint), gt(m.squint, kFloorSquint)), 0);
  add(Reaction::Spin, 0, 0);
  return e;
}

}  // namespace racg::gestures
