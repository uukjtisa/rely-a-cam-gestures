// itsgiving v2's 14 reactions (github.com/itsgiving, MIT), ported line by line from its_giving_v2.py.
//
// Every rule is the same conjunction of thresholds, but each condition yields a ratio that is >= 1
// exactly when itsgiving's condition holds (a < k gives k / a, a > k gives a / k, positions give
// 1 + margin / face width). A rule's score is the smallest of its ratios, so score >= 1 is precisely
// itsgiving's decision; the sensitivity line moves that to score >= line / 60 (match % = 60 x score).
// itsgiving's frame counts (tuned at about 30 fps) become seconds, because detection runs at 15 fps.
#pragma once

#include "vision/observation.hpp"

#include <QJsonObject>
#include <QString>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace racg::gestures {

enum class Reaction {
  TimeOut, Heart, CoverNose, CrashingOut, Dance, NoseClosed, Flirty, HandUp,
  TongueOut, OpenMouth, Disgusted, TalkingToWall, Suspicious, Spin, Count
};
constexpr int kReactionCount = int(Reaction::Count);
const char *reactionId(Reaction r);      // itsgiving's POSES names ("time_out"), also its picture names
const char *reactionName(Reaction r);    // "Time out"
const char *reactionHow(Reaction r);     // "both hands make a T"
float reactionArmSeconds(Reaction r);    // ARM (default 3) frames at 30 fps
constexpr float kHoldSeconds = 10 / 30.f;   // HOLD_FRAMES: how long a reaction lingers once dropped
constexpr float kFaceScale = 2.0f;          // FACE_SCALE: a head meme is 2x the face's height

// Face, Hand and Body as itsgiving reads them (pixels of the analysed, mirrored frame).
struct FaceGeom {
  Box box;
  float w = 0, h = 0;
  Vec2 center, nose, chin, top, mouth;
  float eyeY = 0, turnSigned = 0;
};
struct HandGeom {
  Vec2 palm, thumb, index, middle;
  float horizontal = 0, vertical = 0;   // ratios: >= 1 when itsgiving's flag is true
  float open = 0;                       // >= 1 when at least three fingers are out
};
struct BodyGeom {
  Vec2 shoulders[2], elbows[2], wrists[2];
  float seen = 0;        // min visibility of 11-14 over 0.5: >= 1 when seen
  float elbowsUp = 0;    // >= 1 when both elbows are above the shoulders (and seen)
};
FaceGeom faceGeom(const vision::Face &f);
HandGeom handGeom(const vision::Hand &h);
BodyGeom bodyGeom(const vision::Body &b, float faceWidth);

// Your resting face (itsgiving's calibration.json, version 1, read and written compatibly), plus my
// addition: the neutral arm shape, so a recorded gesture knows whether the body changed.
class Baseline {
 public:
  std::map<std::string, float> mean, sigma;
  int samples = 0;
  QString made;
  std::vector<float> bodyNeutral;   // armVector() at rest; empty if no body was seen
  bool generic() const { return mean.empty(); }
  float z(const std::string &name, float value) const;   // sigma above neutral
  float neutralTurn() const;
  QJsonObject toJson() const;
  static Baseline fromJson(const QJsonObject &o);   // generic if the version or mean is missing
  bool save(const QString &path) const;
  static Baseline load(const QString &path);
};

// Running mean and standard deviation per channel over the calibration window.
class Collector {
 public:
  void add(const vision::Face &face, const vision::Body *body);
  int count() const { return n_; }
  Baseline finish(const QString &made) const;

 private:
  int n_ = 0, bodyN_ = 0;
  std::map<std::string, double> s_, ss_;
  std::vector<double> body_;
};
constexpr float kCalibSeconds = 7.0f, kCalibWarmup = 1.5f;
constexpr int kCalibMinSamples = 30;
// The ways a calibration goes wrong (mid-expression, fidgeting), in plain words.
std::vector<QString> calibrationWarnings(const Baseline &b);

// The body's arm shape: shoulder->elbow and elbow->wrist for both arms over the shoulder width.
std::vector<float> armVector(const vision::Body &b);

struct Measures {
  float jaw = 0, zJaw = 0, sneer = 0, zSneer = 0, zBrow = 0, zFrown = 0, zLip = 0;
  float squint = 0, zSquint = 0, turn = 0, zDisgust = 0;
};
Measures measure(const FaceGeom &g, const vision::Face &f, const Baseline &b);

// itsgiving's tongue test, the pixel half: the fraction of the inner-lip area that reads pink
// (saturated, lit). Runs on the full-resolution camera picture, so `lips` are kInnerLips' points
// already mapped into it. 0 when the mouth is too small to judge (as itsgiving).
constexpr std::array<int, 20> kInnerLips = {78, 95, 88, 178, 87, 14, 317, 402, 318, 324,
                                            308, 415, 310, 311, 312, 13, 82, 81, 80, 191};
float mouthPink(const QImage &full, const std::array<Vec2, 20> &lips);

// Smoothed hand speed in face widths per second (itsgiving's per-frame energy, made per second).
class Motion {
 public:
  float update(const std::vector<HandGeom> &hands, const FaceGeom *face, float dt);
  void reset() { prev_.clear(); energy_ = 0; }

 private:
  std::vector<Vec2> prev_;
  float energy_ = 0, fw_ = 200;
};

struct ReadyEval {
  std::array<float, kReactionCount> score{};   // the best of each reaction's candidates
  // decide()'s candidates in its order; the first whose score passes its own line is the reaction.
  struct Candidate {
    Reaction r;
    float score;
    unsigned hands;   // bit i: the observation's hand i is part of it
  };
  std::vector<Candidate> order;
  Measures m;
  float tongue = 0, gesture = 0;
};
// Uses Observation::mouthPink. Motion is advanced by dt seconds.
ReadyEval evaluate(const vision::Observation &obs, const Baseline &base, Motion &motion, float dt);

}  // namespace racg::gestures
