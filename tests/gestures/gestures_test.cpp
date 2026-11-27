// itsgiving's port, the matcher and the engine's rules.
#include "../check.hpp"

#include "framing/framer.hpp"
#include "gestures/engine.hpp"
#include "gestures/itsgiving.hpp"
#include "gestures/library.hpp"
#include "gestures/matcher.hpp"
#include "gestures/sessions.hpp"
#include "vision/landmarkers.hpp"

#include <QImage>
#include <QTemporaryDir>

#include <cmath>

using namespace racg;
using namespace racg::gestures;
using vision::Observation;
using vision::Sign;

namespace {

vision::Models &models() {
  static vision::Models m(RACG_MODELS, false);
  return m;
}

Observation see(const char *picture) {
  QImage img = QImage(QString(RACG_TEST_DATA "/vision/%1.png").arg(picture)).convertToFormat(QImage::Format_RGB32);
  vision::HandLandmarker hands(models());
  vision::FaceLandmarker faces(models());
  vision::PoseLandmarker poses(models());
  Observation o;
  o.width = img.width();
  o.height = img.height();
  o.frame = img;
  o.hands = hands.process(img);
  o.face = faces.process(img);
  o.body = poses.process(img);
  return o;
}

// A face whose landmarks make a 100 px wide box and whose blendshapes are all `rest`.
vision::Face flatFace(float rest = 0.02f) {
  vision::Face f;
  for (int i = 0; i < 478; ++i) f.lm[i] = {300.f + (i % 11) * 10, 150.f + (i % 13) * 10};
  f.lm[1] = {350, 200};   // nose
  f.lm[234] = {300, 200};
  f.lm[454] = {400, 200};
  for (auto &b : f.blend) b = rest;
  return f;
}

vision::Hand signHand(Sign s, float p, bool left, Vec2 at) {
  vision::Hand h;
  for (int i = 0; i < 21; ++i) h.lm[i] = {at.x + (i % 5) * 4, at.y - (i / 5) * 6};
  h.signs[int(s)] = p;
  h.rightScore = left ? 0.1f : 0.9f;
  return h;
}

Library signLibrary() {
  Library l = Library::defaults();
  l.controls.list.clear();   // plain gestures: no control holds a sign
  l.applyControls();
  for (auto &g : l.gestures) g.on = false;
  auto *v = l.find("sign:Victory");
  v->on = true;
  v->action = "meme:peace";
  v->hold = 0.3f;
  return l;
}

Observation withHands(std::vector<vision::Hand> hs) {
  Observation o;
  o.width = 640;
  o.height = 360;
  o.hands = std::move(hs);
  return o;
}

// A pinching hand whose palm is `size` px (wrist to middle knuckle) at `at`.
vision::Hand pinchHand(Vec2 at, float size) {
  vision::Hand h;
  h.lm[0] = {at.x, at.y + size};
  h.lm[9] = {at.x, at.y};
  h.lm[5] = {at.x - size * 0.4f, at.y};
  h.lm[17] = {at.x + size * 0.4f, at.y};
  h.lm[4] = h.lm[8] = {at.x, at.y - size * 0.6f};
  h.world[9] = {0, -0.09f, 0};
  h.world[8] = {0, -0.13f, 0};   // the index reaches out of the palm: a pinch, not a fist
  return h;
}

struct FramerRun {
  framing::Framer f;
  double t = 0;
  void feed(const std::vector<vision::Hand> &hs, int frames) {
    for (int i = 0; i < frames; ++i) {
      t += 1 / 15.0;
      f.update(withHands(hs), t);
    }
  }
  // Pinch, move from one size to another, settle, let go.
  void pull(float from, float to) {
    const int steps = 8;
    for (int k = 0; k <= steps; ++k) feed({pinchHand({320, 180}, from + (to - from) * k / steps)}, 1);
    feed({pinchHand({320, 180}, to)}, 6);
    feed({}, 4);
  }
  double zoom() const { return 1.0 / f.target().height(); }
};

}  // namespace

TEST(framer_pinch_pulls_zoom_in_and_back_out) {
  FramerRun r;
  r.pull(60, 90);   // pinch and push toward the camera
  const double in = r.zoom();
  CHECK_MSG(in > 1.8, std::to_string(in));
  r.feed({}, 10);
  CHECK(std::abs(r.zoom() - in) < 1e-6);   // let go: it stays
  r.pull(90, 70);   // pinch and pull back: out a bit
  const double mid = r.zoom();
  CHECK_MSG(mid < in - 0.3, std::to_string(mid));
  r.pull(90, 50);   // and again, hand over hand: the whole picture
  CHECK_MSG(r.zoom() < 1.01, std::to_string(r.zoom()));
  r.feed({pinchHand({320, 180}, 60)}, 20);   // a steady pinch doesn't creep
  CHECK(r.zoom() < 1.01);
}

TEST(framer_follow_with_no_face_leaves_the_pinch_to_the_zoom) {
  FramerRun r;
  r.f.setFollow(true);   // on, but no face in the picture
  const double span = r.f.followSpan();
  r.pull(60, 90);
  CHECK_MSG(r.zoom() > 1.8, std::to_string(r.zoom()));
  CHECK(r.f.followSpan() == span);
  r.f.reset();
  CHECK(r.zoom() == 1.0);
}

TEST(itsgiving_open_mouth_threshold_is_score_one) {
  Baseline generic;
  vision::Observation o;
  o.face = flatFace();
  const int jaw = vision::blendshapeIndex("jawOpen");
  // Generic baseline: z = (v - 0.08) / 0.035; z >= 6 needs v >= 0.29, the floor needs v >= 0.30.
  o.face->blend[jaw] = 0.30f;
  Motion m;
  auto e = evaluate(o, generic, m, 1 / 15.f);
  CHECK_MSG(std::abs(e.score[int(Reaction::OpenMouth)] - 1.f) < 1e-4f, std::to_string(e.score[int(Reaction::OpenMouth)]));
  o.face->blend[jaw] = 0.20f;
  e = evaluate(o, generic, m, 1 / 15.f);
  CHECK(e.score[int(Reaction::OpenMouth)] < 1);
}

TEST(itsgiving_spin_when_nobody_is_there) {
  Baseline b;
  Motion m;
  Observation empty;
  auto e = evaluate(empty, b, m, 1 / 15.f);
  CHECK(e.score[int(Reaction::Spin)] * 60 >= 95);
  Observation hand = withHands({signHand(Sign::OpenPalm, 0.9f, false, {300, 200})});
  e = evaluate(hand, b, m, 1 / 15.f);
  CHECK(e.score[int(Reaction::Spin)] == 0);
}

TEST(itsgiving_calibration_round_trips) {
  Collector c;
  vision::Face f = flatFace(0.05f);
  for (int i = 0; i < 40; ++i) {
    f.blend[vision::blendshapeIndex("jawOpen")] = 0.05f + 0.01f * (i % 3);
    c.add(f, nullptr);
  }
  Baseline b = c.finish("2026-11-27 12:00");
  CHECK(!b.generic());
  CHECK(b.samples == 40);
  CHECK(b.sigma.at("noseSneerLeft") == 0.015f);   // SIGMA_FLOOR
  QTemporaryDir dir;
  const QString path = dir.filePath("calibration.json");
  CHECK(b.save(path));
  Baseline back = Baseline::load(path);
  CHECK(back.mean == b.mean && back.sigma == b.sigma && back.samples == 40);
  CHECK(std::abs(back.z("jawOpen", b.mean.at("jawOpen") + 2 * b.sigma.at("jawOpen")) - 2) < 1e-3f);
}

TEST(matcher_take_matches_its_own_sign_only) {
  Baseline b;
  const Observation victory = see("victory"), thumb = see("thumb_up"), point = see("pointing_up");
  TakeBuilder tb;
  for (int i = 0; i < 30; ++i) tb.add(victory, b);
  auto take = tb.finish();
  CHECK(take.has_value());
  if (!take) return;
  Parts p = defaultParts({*take}, b);
  CHECK(p.rightHand && !p.leftHand);
  const float self = matchCustom({*take}, p, victory, b).score * 60;
  const float other1 = matchCustom({*take}, p, thumb, b).score * 60;
  const float other2 = matchCustom({*take}, p, point, b).score * 60;
  std::printf("  victory take: itself %.0f %%, thumbs up %.0f %%, pointing up %.0f %%\n", self, other1, other2);
  CHECK(self >= 95);
  CHECK(other1 < 45 && other2 < 45);
  // Round trip through JSON keeps the match.
  Take back = Take::fromJson(take->toJson());
  CHECK(std::abs(matchCustom({back}, p, victory, b).score * 60 - self) < 0.5f);
}

TEST(engine_hold_then_linger_then_end) {
  Library l = signLibrary();
  Engine e(l);
  Baseline b;
  const Observation on = withHands({signHand(Sign::Victory, 0.9f, false, {300, 200})});
  const Observation off = withHands({});
  double t = 0;
  auto step = [&](const Observation &o) { t += 1 / 15.0; return e.update(o, b, t); };
  CHECK(step(on).empty());        // 1 frame = 0.067 s
  CHECK(step(on).empty());        // 0.133 s
  CHECK(step(on).empty());        // 0.2 s
  CHECK(step(on).empty());        // 0.267 s
  auto c = step(on);              // 0.333 s >= 0.3
  CHECK(c.size() == 1 && c[0].type == Command::Start && c[0].action == "meme:peace");
  CHECK(step(on).empty());        // held: no refire
  // Lingers 1/3 s (five frames at 15 fps; the sixth if the float lands just under).
  for (int i = 0; i < 4; ++i) CHECK(step(off).empty());
  c = step(off);
  if (c.empty()) c = step(off);
  CHECK(c.size() == 1 && c[0].type == Command::End && c[0].reason == "let go");
}

TEST(engine_again_toggles_and_after_times_out) {
  Library l = signLibrary();
  auto *v = l.find("sign:Victory");
  v->ends = Ends::Again;
  v->hold = 0.1f;
  Engine e(l);
  Baseline b;
  const Observation on = withHands({signHand(Sign::Victory, 0.9f, false, {300, 200})});
  const Observation off = withHands({});
  double t = 0;
  int starts = 0, ends = 0;
  auto run = [&](const Observation &o, int frames) {
    for (int i = 0; i < frames; ++i) {
      t += 1 / 15.0;
      for (const auto &c : e.update(o, b, t)) (c.type == Command::Start ? starts : ends)++;
    }
  };
  run(on, 5);
  CHECK(starts == 1 && ends == 0);
  run(off, 10);
  CHECK(ends == 0);               // "again": letting go doesn't end it
  run(on, 5);
  CHECK(starts == 1 && ends == 1);
  v->ends = Ends::After;
  v->secs = 2;
  run(off, 10);
  run(on, 3);
  CHECK(starts == 2);
  run(off, 31);                   // 2 s later
  CHECK(ends == 2);
}

TEST(engine_confirm_needs_the_other_hand) {
  Library l = signLibrary();
  l.find("sign:Victory")->confirm = true;
  l.find("sign:Thumb_Up")->action = "snap";
  l.find("sign:Thumb_Up")->on = true;
  Engine e(l);
  Baseline b;
  const auto peace = signHand(Sign::Victory, 0.9f, false, {300, 200});
  auto thumbs = signHand(Sign::ThumbUp, 0.9f, true, {100, 200});
  double t = 0;
  std::vector<Command> got;
  auto run = [&](const Observation &o, int frames) {
    for (int i = 0; i < frames; ++i) {
      t += 1 / 15.0;
      for (const auto &c : e.update(o, b, t)) got.push_back(c);
    }
  };
  run(withHands({peace}), 10);
  CHECK(got.empty());
  CHECK(e.state("sign:Victory")->waitingConfirm);
  run(withHands({peace, thumbs}), 2);
  CHECK(got.size() == 1 && got[0].gesture == "sign:Victory");   // and Thumbs up didn't fire as itself
}

TEST(engine_stop_sign_ends_what_listens) {
  Library l = signLibrary();
  auto *v = l.find("sign:Victory");
  v->ends = Ends::Again;
  v->hold = 0.1f;
  Engine e(l);
  Baseline b;
  double t = 0;
  std::vector<Command> got;
  auto run = [&](const Observation &o, int frames) {
    for (int i = 0; i < frames; ++i) {
      t += 1 / 15.0;
      for (const auto &c : e.update(o, b, t)) got.push_back(c);
    }
  };
  run(withHands({signHand(Sign::Victory, 0.9f, false, {300, 200})}), 4);
  CHECK(got.size() == 1);
  const auto palm = [](bool left, float x) { return signHand(Sign::OpenPalm, 0.9f, left, {x, 200}); };
  run(withHands({palm(true, 100), palm(false, 500)}), 10);   // 0.67 s: not yet
  CHECK(got.size() == 1);
  run(withHands({palm(true, 100), palm(false, 500)}), 6);    // past 1 s
  CHECK(got.size() == 2 && got[1].type == Command::End && got[1].reason == "stop sign");
}

TEST(engine_pause_lets_only_the_pause_toggle_through) {
  Library l = signLibrary();
  l.find("sign:Thumb_Up")->action = Engine::kPauseAction;
  l.find("sign:Thumb_Up")->on = true;
  Engine e(l);
  e.setPaused(true);
  Baseline b;
  double t = 0;
  std::vector<Command> got;
  for (int i = 0; i < 10; ++i) {
    t += 1 / 15.0;
    for (const auto &c : e.update(withHands({signHand(Sign::Victory, 0.9f, false, {300, 200})}), b, t)) got.push_back(c);
  }
  CHECK(got.empty());
  for (int i = 0; i < 10; ++i) {
    t += 1 / 15.0;
    for (const auto &c : e.update(withHands({signHand(Sign::ThumbUp, 0.9f, false, {300, 200})}), b, t)) got.push_back(c);
  }
  CHECK(got.size() == 1 && got[0].action == Engine::kPauseAction);
}

TEST(engine_one_shot_fires_every_time) {
  Library l = signLibrary();
  auto *v = l.find("sign:Victory");
  v->action = "snap";
  v->ends = Ends::Again;
  v->hold = 0.1f;
  Engine e(l);
  EngineConfig c;
  c.oneShot = [](const QString &a) { return a == "snap"; };
  e.setConfig(c);
  Baseline b;
  double t = 0;
  int starts = 0;
  auto run = [&](bool on, int frames) {
    for (int i = 0; i < frames; ++i) {
      t += 1 / 15.0;
      auto o = on ? withHands({signHand(Sign::Victory, 0.9f, false, {300, 200})}) : withHands({});
      for (const auto &cmd : e.update(o, b, t)) starts += cmd.type == Command::Start;
    }
  };
  run(true, 4);
  run(false, 8);
  run(true, 4);
  CHECK(starts == 2);
}

TEST(controls_a_released_gesture_is_plain_again) {
  Library l = Library::defaults();
  const auto *v = l.find("sign:Victory");
  CHECK(v->action == "obs.record" && v->confirm && v->ends == Ends::Again);
  l.controls.setTrigger("obs.record", "sign:ILoveYou");
  l.applyControls();
  v = l.find("sign:Victory");
  CHECK(v->action.isEmpty() && !v->confirm && v->ends == Ends::Held && v->on);
  CHECK(l.find("sign:ILoveYou")->action == "obs.record" && l.find("sign:ILoveYou")->confirm);
  // The two pinches that read the same move switch each other off.
  CHECK(!l.controls.setOn("zoom.hold", true).isEmpty());
  CHECK(!l.controls.moveOn("pinch.zoom") && l.controls.moveOn("zoom.hold"));
}

TEST(library_round_trips_settings_and_takes) {
  Library l = Library::defaults();
  CHECK(l.gestures.size() == 21);
  l.find("ready:heart")->action = "meme:heart";
  l.find("ready:heart")->sens = 72;
  auto &m = l.addMine("Peace, record");
  m.parts.rightHand = true;
  Take t;
  t.hand[1].seen = true;
  t.hand[1].embedding[3] = 1;
  m.takes.push_back(t);
  const QString id = m.id;
  Library back = Library::fromJson(l.toJson());
  CHECK(back.gestures.size() == 22);
  CHECK(back.find("ready:heart")->action == "meme:heart" && back.find("ready:heart")->sens == 72);
  CHECK(back.find(id) && back.find(id)->takes.size() == 1 && back.find(id)->parts.rightHand);
  CHECK(back.find(id)->takes[0].hand[1].embedding[3] == 1);
}

TEST(tester_counts_misfires) {
  Library l = signLibrary();
  GestureDef &g = *l.find("sign:Victory");
  g.hold = 0.2f;
  Tester tst;
  tst.startMisfire(g.id, 0);
  GestureState s;
  double t = 0;
  for (int i = 0; i < 151; ++i) {
    t += 1 / 15.0;
    s.pct = (i % 30) < 6 ? 80.f : 10.f;   // made for 0.4 s every 2 s: five times
    tst.add(g, &s, t);
  }
  CHECK(tst.mode() == Tester::Mode::Off);
  CHECK_MSG(tst.fires() == 5, std::to_string(tst.fires()));
}
