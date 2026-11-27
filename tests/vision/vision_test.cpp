// The ONNX pipeline against Google's MediaPipe on the same pictures (tests/data/vision).
#include "../check.hpp"

#include "vision/landmarkers.hpp"

#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <memory>

using namespace racg;
using namespace racg::vision;

namespace {

QJsonObject reference() {
  QFile f(RACG_TEST_DATA "/vision/reference.json");
  f.open(QIODevice::ReadOnly);
  return QJsonDocument::fromJson(f.readAll()).object();
}

std::vector<Vec2> points(const QJsonArray &a) {
  std::vector<Vec2> v;
  for (const auto &p : a) v.push_back({float(p.toArray()[0].toDouble()), float(p.toArray()[1].toDouble())});
  return v;
}

float extent(const std::vector<Vec2> &p) {
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto q : p) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); }
  return std::max(x1 - x0, y1 - y0);
}

// Mean landmark error over the object's size.
template <class L>
float relError(const std::vector<Vec2> &ref, const L &ours, size_t n) {
  float s = 0;
  for (size_t i = 0; i < n; ++i) s += dist(ref[i], ours[i].xy());
  return s / n / extent(ref);
}

Models &models() {
  static Models m(RACG_MODELS, false);   // the CPU, so the test is the same on every machine
  return m;
}

}  // namespace

// The embedding custom gestures match on: the same sign must sit closer than different ones.
TEST(vision_embedding_separates_signs) {
  struct H { std::string name; Sign sign; std::array<float, 128> e; };
  std::vector<H> all;
  HandLandmarker hands(models());
  for (const char *f : {"fist", "left_hands", "pointing_up", "right_hands", "thumb_up", "victory"}) {
    QImage img(QString(RACG_TEST_DATA "/vision/%1.png").arg(f));
    hands.reset();
    int i = 0;
    for (const auto &h : hands.process(img.convertToFormat(QImage::Format_RGB32)))
      all.push_back({std::string(f) + "#" + std::to_string(i++), h.sign(), h.embedding});
  }
  auto cosine = [](const auto &a, const auto &b) {
    float d = 0, na = 0, nb = 0;
    for (int i = 0; i < 128; ++i) { d += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return d / std::sqrt(na * nb + 1e-12f);
  };
  float sameMin = 1, diffMax = -1;
  for (size_t i = 0; i < all.size(); ++i)
    for (size_t j = i + 1; j < all.size(); ++j) {
      const float c = cosine(all[i].e, all[j].e);
      const bool same = all[i].sign == all[j].sign && all[i].sign != Sign::None;
      if (qEnvironmentVariableIsSet("RACG_VERBOSE"))
        std::printf("    %-14s %-14s %s %.3f\n", all[i].name.c_str(), all[j].name.c_str(), same ? "same" : "diff", c);
      if (same) sameMin = std::min(sameMin, c);
      else if (all[i].sign != Sign::None && all[j].sign != Sign::None) diffMax = std::max(diffMax, c);
    }
  std::printf("  same sign min %.3f, different signs max %.3f\n", sameMin, diffMax);
  CHECK(sameMin > diffMax);
}

TEST(vision_matches_mediapipe) {
  const QJsonObject ref = reference();
  CHECK(!ref.isEmpty());
  HandLandmarker hands(models());
  FaceLandmarker faces(models());
  PoseLandmarker poses(models());
  for (auto it = ref.begin(); it != ref.end(); ++it) {
    const QString name = it.key();
    const QJsonObject r = it.value().toObject();
    QImage img(RACG_TEST_DATA "/vision/" + name);
    CHECK_MSG(!img.isNull(), name.toStdString());
    img = img.convertToFormat(QImage::Format_RGB32);
    hands.reset();
    faces.reset();
    poses.reset();
    const auto hs = hands.process(img);
    const auto f = faces.process(img);
    const auto b = poses.process(img);
    const std::string n = name.toStdString();

    // Face: count, landmarks, blendshapes.
    const QJsonArray rf = r["face"].toArray();
    CHECK_MSG(int(bool(f)) == rf.size(), n + " face count");
    if (f && rf.size() == 1) {
      const auto rp = points(rf[0].toArray());
      const float e = relError(rp, f->lm, 478);
      float bmean = 0, bmax = 0;
      const QJsonObject rb = r["blend"].toArray()[0].toObject();
      for (auto bi = rb.begin(); bi != rb.end(); ++bi) {
        const float d = std::abs(float(bi.value().toDouble()) - f->blend[blendshapeIndex(bi.key().toStdString())]);
        bmean += d / rb.size();
        bmax = std::max(bmax, d);
      }
      std::printf("  %s face: landmarks %.4f, blendshapes mean %.4f max %.4f\n", n.c_str(), e, bmean, bmax);
      CHECK_MSG(e < 0.02f, n + " face landmarks");
      CHECK_MSG(bmean < 0.02f && bmax < 0.12f, n + " blendshapes");
    }

    // Hands: count, then each reference hand against our nearest one.
    const QJsonArray rh = r["hands"].toArray();
    CHECK_MSG(int(hs.size()) == rh.size(), n + " hand count " + std::to_string(hs.size()));
    for (const auto &h : hs)
      std::printf("  %s found hand: presence %.3f, crop %.0fx%.0f at %.0f,%.0f\n", n.c_str(), h.presence,
                  h.roi.w, h.roi.h, h.roi.cx, h.roi.cy);
    for (int i = 0; i < rh.size() && !hs.empty(); ++i) {
      const auto rp = points(rh[i].toArray());
      const Hand *best = nullptr;
      for (const auto &h : hs)
        if (!best || dist(h.lm[0].xy(), rp[0]) < dist(best->lm[0].xy(), rp[0])) best = &h;
      const float e = relError(rp, best->lm, 21);
      const QString side = r["handedness"].toArray()[i].toArray()[0].toArray()[0].toString();
      const QJsonArray rg = r["gestures"].toArray();
      const QString gesture = i < rg.size() ? rg[i].toArray()[0].toArray()[0].toString() : "None";
      std::printf("  %s hand %d: landmarks %.4f, %s (ref %s), sign %s %.2f (ref %s)\n", n.c_str(), i, e,
                  best->isLeft() ? "Left" : "Right", qPrintable(side), signId(best->sign()),
                  best->signs[int(best->sign())], qPrintable(gesture));
      CHECK_MSG(e < 0.04f, n + " hand landmarks");
      CHECK_MSG((best->isLeft() ? "Left" : "Right") == side, n + " handedness");
      CHECK_MSG(signId(best->sign()) == gesture || (gesture == "None" && best->sign() == Sign::None) ||
                    gesture == "None",
                n + " sign");
    }

    // Pose: count and the visible landmarks.
    const QJsonArray rpz = r["pose"].toArray();
    CHECK_MSG(int(bool(b)) == rpz.size(), n + " pose count");
    if (b && rpz.size() == 1) {
      const QJsonArray pts = rpz[0].toArray();
      std::vector<Vec2> all = points(pts);
      float s = 0;
      int k = 0;
      for (int i = 0; i < 33; ++i)
        if (pts[i].toArray()[2].toDouble() > 0.5) {
          s += dist(all[i], b->lm[i].xy());
          ++k;
          if (qEnvironmentVariableIsSet("RACG_VERBOSE"))
            std::printf("    pose %2d: ref %.1f,%.1f ours %.1f,%.1f  d %.2f\n", i, all[i].x, all[i].y,
                        b->lm[i].x, b->lm[i].y, dist(all[i], b->lm[i].xy()));
        }
      if (qEnvironmentVariableIsSet("RACG_VERBOSE"))
        std::printf("    pose roi %.1f,%.1f %.1fx%.1f rot %.4f\n", b->roi.cx, b->roi.cy, b->roi.w, b->roi.h,
                    b->roi.rotation);
      const float e = k ? s / k / extent(all) : 0;
      std::printf("  %s pose: %d visible landmarks, error %.4f\n", n.c_str(), k, e);
      CHECK_MSG(e < 0.04f, n + " pose landmarks");
    }
  }
}
