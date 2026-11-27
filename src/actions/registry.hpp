// Every action a gesture can do, grouped as the editor's Action menu lists them, and which
// situation each needs. The menu only offers what this situation can do; an action whose situation
// goes away stays on its gesture, greyed with "needs OBS" / "needs Rely-A-Cam", and comes back.
//
// Ids: "meme:<id>", "camera.off", "zoom.in", "zoom.out", "zoom.box", "zoom.hold", "follow.face",
// "snapshot", "phone.mic", "phone.miconly", "phone.torch", "phone.switch", "obs.scene|<name>",
// "obs.record", "obs.stream", "obs.hotkey|<name>", "overlay.grid", "overlay.detection",
// "gestures.pause", "gestures.undo".
#pragma once

#include "memes/library.hpp"

#include <QString>
#include <QStringList>

#include <vector>

namespace racg::actions {

enum class Situation { Own, OwnObs, Rely };   // on its own; on its own with OBS open; inside Rely-A-Cam
enum class Need { None, Obs, Rely };

bool available(Need n, Situation s);
QString needText(Need n);                     // "needs OBS"

struct Item {
  QString id;
  QString label;
  QStringList subs;   // scenes or hotkeys: the id becomes "<id>|<sub>"
};
struct Group {
  QString name;
  Need need = Need::None;
  std::vector<Item> items;
};

// The Action menu's groups for this situation (labels follow it). Scenes and hotkeys come from OBS.
std::vector<Group> groups(Situation s, const memes::Library &memes, const QStringList &scenes,
                          const QStringList &hotkeys);

struct Info {
  QString label;          // "Meme: heart.gif ♪", "Switch scene: Gaming", "None"
  Need need = Need::None;
  bool oneShot = false;   // it just happens (no end): snapshot, zoom steps, scene, hotkey, undo, pause
  bool known = true;      // false for an id this version doesn't know, or a meme that was deleted
};
Info info(const QString &id, Situation s, const memes::Library &memes);
bool isOneShot(const QString &id);
bool isMeme(const QString &id);
QString memeId(const QString &id);
// The action's default for a newly picked action: memes end when let go, the rest toggle.
bool defaultConfirm(const QString &id);   // record and stream ask for the confirm sign

}  // namespace racg::actions
