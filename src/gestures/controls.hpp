// The Controls tab: a firm list of jobs (framing, camera and mic, OBS, the app itself), each run by
// ONE gesture, or by a built-in move for the pinch ones. Reactions (the Reactions tab) only show
// memes. Controls are saved beside the gestures and projected onto them (apply): a control's
// trigger gesture gets the control's action, so a gesture can never have two jobs, and the engine
// stays one engine.
//
// Conflicts: two controls that read the same move can't both be on. Turning one on turns the
// other off (Pinch zoom and Zoom on what I hold are both "one hand pinching, held still").
#pragma once

#include "gestures/gesture.hpp"

#include <QJsonArray>
#include <QString>

#include <vector>

namespace racg::gestures {

enum class ControlGroup { Framing, CameraMic, Obs, Always };

struct ControlSpec {
  const char *id;          // the action it runs; "obs.scene" and "obs.hotkey" are added per name
  ControlGroup group;
  const char *label;
  const char *how;         // the row's small print
  const char *trigger;     // its gesture out of the box; "" for none; nullptr for a built-in move
  bool on;                 // out of the box
  bool confirm;            // needs the confirm sign from the other hand
  Ends ends;               // how its action ends (one-shots ignore it)
  float hold;              // the hold time its gesture starts with (the owner may change it)
  const char *conflicts;   // the control it can't run with, or ""
  bool addable;            // a row per scene or hotkey, added by hand
};

const ControlSpec *controlSpec(const QString &id);   // by the id before '|'
const std::vector<ControlSpec> &controlSpecs();
QString controlLabel(const QString &id);              // "Scene: Gaming" for an added one
bool isMove(const QString &id);                       // a built-in pinch move (no gesture)

struct Control {
  QString id;
  bool on = false;
  QString trigger;   // a gesture id; empty for none (and always for a move)
};

class Controls {
 public:
  std::vector<Control> list;   // the catalogue's order, then added scenes and hotkeys

  static Controls defaults();
  // Saved controls, or (a library from before Controls) the defaults with each gesture's old
  // control action carried over to its control, so nothing the owner set up is lost.
  static Controls fromJson(const QJsonValue &v, const std::vector<GestureDef> &gestures);
  QJsonArray toJson() const;

  Control *find(const QString &id);
  const Control *find(const QString &id) const;
  const Control *byTrigger(const QString &gestureId) const;
  bool moveOn(const QString &id) const;

  // Returns the label of the control it turned off because of a conflict, or empty.
  QString setOn(const QString &id, bool on);
  // The gesture leaves any other control; empty clears it.
  void setTrigger(const QString &id, const QString &gestureId);
  void add(const QString &id);      // "obs.scene|<name>" or "obs.hotkey|<name>"
  void remove(const QString &id);   // only added ones

  // Every non-meme action on a gesture comes from here: cleared, then set from the controls.
  void apply(std::vector<GestureDef> &gestures) const;
};

}  // namespace racg::gestures
