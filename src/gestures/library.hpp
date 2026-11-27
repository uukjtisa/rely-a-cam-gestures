// The gestures: itsgiving's 14 reactions and Google's 7 signs (fixed, with your settings on them)
// and your own (recorded). Saved as JSON in the library folder, so it's easy to back up and the
// phone can browse it.
#pragma once

#include "gestures/itsgiving.hpp"
#include "gestures/matcher.hpp"

#include <QJsonObject>
#include <QString>

#include <vector>

namespace racg::gestures {

enum class Kind { Ready, Sign, Mine };
enum class Ends { Held, Again, After };   // while held (+1/3 s), same gesture again, after N s

constexpr int kSensMin = 30, kSensMax = 95, kSensDefault = 60;
constexpr float kHoldChoices[] = {0.1f, 0.2f, 0.3f, 0.5f, 1.0f, 2.0f};

struct GestureDef {
  QString id;            // "ready:time_out", "sign:Thumb_Up", "mine:<uuid>"
  Kind kind = Kind::Mine;
  int ref = 0;           // the Reaction or Sign it is
  QString name, icon;
  bool on = true;
  QString action;        // an action id (actions/registry.hpp), empty for none
  Ends ends = Ends::Held;
  int secs = 3;          // for Ends::After
  bool stop = true;      // the stop sign ends it
  bool confirm = false;  // needs the confirm sign from the other hand
  int sens = kSensDefault;   // the match line, kSensMin..kSensMax
  float hold = 0.1f;     // seconds it must be held before it fires
  Parts parts;           // what counts (own gestures; ready-made ones show their rule's parts)
  std::vector<Take> takes;

  QString how() const;   // the row's small print when it has no action
  Reaction reaction() const { return Reaction(ref); }
  vision::Sign sign() const { return vision::Sign(ref); }
  // Which hands its own match uses can't be known until it matches; this is what it is built from.
  bool usesBothHands() const;
};

class Library {
 public:
  std::vector<GestureDef> gestures;

  static Library defaults();
  GestureDef *find(const QString &id);
  const GestureDef *find(const QString &id) const;
  GestureDef &addMine(const QString &name);
  void remove(const QString &id);

  QJsonObject toJson() const;
  static Library fromJson(const QJsonObject &o);   // defaults with the saved settings laid over them
  bool save(const QString &path) const;
  static Library load(const QString &path);
};

}  // namespace racg::gestures
