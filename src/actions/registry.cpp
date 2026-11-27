#include "actions/registry.hpp"

namespace racg::actions {

bool available(Need n, Situation s) {
  switch (n) {
    case Need::None: return true;
    case Need::Obs: return s != Situation::Own;
    case Need::Rely: return s == Situation::Rely;
  }
  return false;
}

QString needText(Need n) {
  return n == Need::Obs ? "needs OBS" : n == Need::Rely ? "needs Rely-A-Cam" : QString();
}

std::vector<Group> groups(Situation s, const memes::Library &memes, const QStringList &scenes,
                          const QStringList &hotkeys) {
  std::vector<Group> g;
  Group meme{"Meme", Need::None, {}};
  for (const auto &m : memes.memes)
    meme.items.push_back({"meme:" + m.id, "Meme: " + m.name() + (m.hasSound() ? " ♪" : ""), {}});
  g.push_back(meme);
  g.push_back({"Camera",
               Need::None,
               {{"camera.off", s == Situation::Rely ? "Camera off (phone: mic only)" : "Camera off (output goes dark)", {}},
                {"zoom.in", "Zoom in (next preset)", {}},
                {"zoom.out", "Zoom out (previous preset)", {}},
                {"zoom.box", "Two pinches draw a box", {}},
                {"zoom.hold", "Zoom on what I hold", {}},
                {"follow.face", "Follow face", {}},
                {"snapshot", "Snapshot", {}}}});
  g.push_back({"Phone",
               Need::Rely,
               {{"phone.mic", "Mic on/off", {}},
                {"phone.miconly", "Mic only", {}},
                {"phone.torch", "Torch", {}},
                {"phone.switch", "Switch front/back camera", {}}}});
  g.push_back({"OBS",
               Need::Obs,
               {{"obs.scene", "Switch scene", scenes},
                {"obs.record", "Start/stop recording", {}},
                {"obs.stream", "Start/stop streaming", {}},
                {"obs.hotkey", "Any OBS hotkey", hotkeys}}});
  g.push_back({"Overlays", Need::None, {{"overlay.grid", "Grid on/off", {}}, {"overlay.detection", "Detection overlay on/off", {}}}});
  g.push_back({"Gestures", Need::None, {{"gestures.pause", "Pause gestures", {}}, {"gestures.undo", "Undo last action", {}}}});
  return g;
}

bool isMeme(const QString &id) { return id.startsWith("meme:"); }
QString memeId(const QString &id) { return id.mid(5); }

bool isOneShot(const QString &id) {
  const QString base = id.section('|', 0, 0);
  return base == "zoom.in" || base == "zoom.out" || base == "snapshot" || base == "obs.scene" ||
         base == "obs.hotkey" || base == "gestures.pause" || base == "gestures.undo";
}

bool defaultConfirm(const QString &id) { return id == "obs.record" || id == "obs.stream"; }

Info info(const QString &id, Situation s, const memes::Library &memes) {
  Info r;
  if (id.isEmpty()) {
    r.label = "None";
    return r;
  }
  r.oneShot = isOneShot(id);
  if (isMeme(id)) {
    const memes::Meme *m = memes.find(memeId(id));
    r.known = m != nullptr;
    r.label = m ? "Meme: " + m->name() + (m->hasSound() ? " ♪" : "") : "Meme: (deleted)";
    return r;
  }
  const QString base = id.section('|', 0, 0), sub = id.section('|', 1);
  for (const auto &g : groups(s, memes, {}, {}))
    for (const auto &it : g.items)
      if (it.id == base) {
        r.label = it.label + (sub.isEmpty() ? QString() : ": " + sub);
        r.need = g.need;
        return r;
      }
  r.known = false;
  r.label = id;
  return r;
}

}  // namespace racg::actions
