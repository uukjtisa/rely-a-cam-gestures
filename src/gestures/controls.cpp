#include "gestures/controls.hpp"
#include "gestures/itsgiving.hpp"

#include <QJsonObject>

#include <algorithm>

namespace racg::gestures {

namespace {

using G = ControlGroup;

// The catalogue, in the tab's order. Out-of-the-box gestures are Google's signs; the confirm sign
// (Thumb_Up) and the stop sign (both palms) are never used here.
const std::vector<ControlSpec> kSpecs = {
    {"pinch.zoom", G::Framing, "Pinch zoom",
     "pinch and pull back to zoom out, or push toward the camera to zoom in; let go and pinch again to keep going",
     nullptr, true, false, Ends::Held, 0, "zoom.hold", false},
    {"zoom.box", G::Framing, "Pinch box", "pinch with both hands at once: the box between them fills the picture", nullptr,
     true, false, Ends::Held, 0, "", false},
    {"zoom.hold", G::Framing, "Zoom on what I hold",
     "pinch and hold still for 0.7 s: the picture follows what you hold until you let go", nullptr, false, false, Ends::Held,
     0, "pinch.zoom", false},
    {"follow.face", G::Framing, "Follow face",
     "tap: on or off. Hold it and pinch-zoom with the other hand: how close (stays on)", "sign:Pointing_Up", true, false,
     Ends::Held, 0.3f, "", false},
    {"zoom.in", G::Framing, "Zoom in a step", "1x, 1.5x, 2x, 3x", "", false, false, Ends::Held, 0.3f, "", false},
    {"zoom.out", G::Framing, "Zoom out a step", "", "", false, false, Ends::Held, 0.3f, "", false},
    {"camera.off", G::CameraMic, "Camera off", "again to turn it back on", "sign:Closed_Fist", true, false, Ends::Again, 0.6f,
     "", false},
    {"phone.mic", G::CameraMic, "Mute the phone's mic", "again to unmute", "sign:Thumb_Down", false, false, Ends::Again, 0.5f,
     "", false},
    {"phone.miconly", G::CameraMic, "Mic only", "the phone stops its camera, keeps its mic", "", false, false, Ends::Again,
     0.5f, "", false},
    {"phone.torch", G::CameraMic, "Torch", "", "", false, false, Ends::Again, 0.5f, "", false},
    {"phone.switch", G::CameraMic, "Switch front/back camera", "", "", false, false, Ends::Held, 0.5f, "", false},
    {"obs.record", G::Obs, "Record", "start or stop", "sign:Victory", true, true, Ends::Again, 0.3f, "", false},
    {"obs.stream", G::Obs, "Stream", "go live or stop", "", false, true, Ends::Again, 0.3f, "", false},
    {"obs.scene.next", G::Obs, "Next scene", "", "", false, false, Ends::Held, 0.3f, "", false},
    {"snapshot", G::Obs, "Snapshot", "", "", false, false, Ends::Held, 0.3f, "", false},
    {"obs.scene", G::Obs, "Scene", "", "", false, false, Ends::Held, 0.3f, "", true},
    {"obs.hotkey", G::Obs, "Hotkey", "", "", false, false, Ends::Held, 0.3f, "", true},
    {"gestures.pause", G::Always, "Pause gestures", "again to resume", "", false, false, Ends::Again, 0.5f, "", false},
    {"gestures.undo", G::Always, "Undo the last action", "", "", false, false, Ends::Held, 0.5f, "", false},
    {"overlay.detection", G::Always, "Detection overlay", "on or off", "", false, false, Ends::Again, 0.5f, "", false},
    {"overlay.grid", G::Always, "Grid", "on or off", "", false, false, Ends::Again, 0.5f, "", false},
};

bool isMeme(const QString &action) { return action.startsWith("meme:"); }

// A gesture's own hold, before any control raises it.
float ownHold(const GestureDef &g) { return g.kind == Kind::Ready ? reactionArmSeconds(g.reaction()) : 0.3f; }

GestureDef *gestureById(std::vector<GestureDef> &gs, const QString &id) {
  for (auto &g : gs)
    if (g.id == id) return &g;
  return nullptr;
}

}  // namespace

const std::vector<ControlSpec> &controlSpecs() { return kSpecs; }

const ControlSpec *controlSpec(const QString &id) {
  const QString base = id.section('|', 0, 0);
  for (const auto &s : kSpecs)
    if (base == s.id) return &s;
  return nullptr;
}

QString controlLabel(const QString &id) {
  const ControlSpec *s = controlSpec(id);
  if (!s) return id;
  const QString sub = id.section('|', 1);
  return sub.isEmpty() ? QString(s->label) : QString(s->label) + ": " + sub;
}

bool isMove(const QString &id) {
  const ControlSpec *s = controlSpec(id);
  return s && !s->trigger;
}

Controls Controls::defaults() {
  Controls c;
  for (const auto &s : kSpecs)
    if (!s.addable) c.list.push_back({s.id, s.on, s.trigger ? QString(s.trigger) : QString()});
  return c;
}

Controls Controls::fromJson(const QJsonValue &v, const std::vector<GestureDef> &gestures) {
  Controls c = defaults();
  if (v.isArray()) {
    for (const auto &e : v.toArray()) {
      const QJsonObject o = e.toObject();
      const QString id = o["id"].toString();
      const ControlSpec *s = controlSpec(id);
      if (!s) continue;   // from a newer version
      Control *k = c.find(id);
      if (!k) {
        if (!s->addable) continue;
        c.list.push_back({id, false, {}});
        k = &c.list.back();
      }
      k->on = o["on"].toBool(k->on);
      k->trigger = s->trigger ? o["trigger"].toString() : QString();
    }
    return c;
  }
  // Before Controls: a default never claims a gesture that already did something else...
  for (auto &k : c.list)
    for (const auto &g : gestures)
      if (g.id == k.trigger && !g.action.isEmpty() && !isMeme(g.action)) k.trigger.clear();
  // ...and a gesture's own control action becomes that control's gesture.
  for (const auto &g : gestures) {
    if (g.action.isEmpty() || isMeme(g.action)) continue;
    const ControlSpec *s = controlSpec(g.action);
    if (!s || !s->trigger) continue;   // the pinch ones are moves now, on by themselves
    if (s->addable) c.add(g.action);
    c.setTrigger(g.action, g.id);
    if (Control *k = c.find(g.action)) k->on = g.on;
  }
  // An out-of-the-box gesture that already shows a meme keeps its meme.
  for (auto &k : c.list)
    if (!k.trigger.isEmpty())
      for (const auto &g : gestures)
        if (g.id == k.trigger && isMeme(g.action)) k.trigger.clear();
  return c;
}

QJsonArray Controls::toJson() const {
  QJsonArray a;
  for (const auto &k : list) {
    QJsonObject o{{"id", k.id}, {"on", k.on}};
    if (!isMove(k.id)) o["trigger"] = k.trigger;
    a.append(o);
  }
  return a;
}

Control *Controls::find(const QString &id) {
  for (auto &k : list)
    if (k.id == id) return &k;
  return nullptr;
}
const Control *Controls::find(const QString &id) const { return const_cast<Controls *>(this)->find(id); }

const Control *Controls::byTrigger(const QString &gestureId) const {
  if (gestureId.isEmpty()) return nullptr;
  for (const auto &k : list)
    if (k.trigger == gestureId) return &k;
  return nullptr;
}

bool Controls::moveOn(const QString &id) const {
  const Control *k = find(id);
  return k && k->on;
}

QString Controls::setOn(const QString &id, bool on) {
  Control *k = find(id);
  if (!k) return {};
  k->on = on;
  const ControlSpec *s = controlSpec(id);
  if (!on || !s || !*s->conflicts) return {};
  Control *other = find(s->conflicts);
  if (!other || !other->on) return {};
  other->on = false;
  return controlLabel(other->id);
}

void Controls::setTrigger(const QString &id, const QString &gestureId) {
  if (!gestureId.isEmpty())
    for (auto &k : list)
      if (k.trigger == gestureId) k.trigger.clear();
  if (Control *k = find(id); k && !isMove(id)) k->trigger = gestureId;
}

void Controls::add(const QString &id) {
  const ControlSpec *s = controlSpec(id);
  if (!s || !s->addable || id.section('|', 1).isEmpty() || find(id)) return;
  list.push_back({id, true, {}});
}

void Controls::remove(const QString &id) {
  const ControlSpec *s = controlSpec(id);
  if (!s || !s->addable) return;
  std::erase_if(list, [&](const Control &k) { return k.id == id; });
}

void Controls::apply(std::vector<GestureDef> &gestures) const {
  for (auto &g : gestures) {
    if (g.action.isEmpty() || isMeme(g.action) || byTrigger(g.id)) continue;
    // Its control moved away: nothing of that job stays, or a meme put here later would wait for
    // the job's confirm sign, keep its timing, or be switched off.
    g.action.clear();
    g.on = true;
    g.ends = Ends::Held;
    g.confirm = false;
    g.hold = ownHold(g);
  }
  for (const auto &k : list) {
    const ControlSpec *s = controlSpec(k.id);
    if (!s || !s->trigger || k.trigger.isEmpty()) continue;
    GestureDef *g = gestureById(gestures, k.trigger);
    if (!g) continue;
    if (g->action != k.id) g->hold = ownHold(*g);   // newly given this job
    g->action = k.id;
    g->on = k.on;
    g->ends = s->ends;
    g->confirm = s->confirm;
    g->stop = true;
    g->hold = std::max(g->hold, s->hold);
  }
}

}  // namespace racg::gestures
