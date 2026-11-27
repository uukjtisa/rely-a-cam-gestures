// One gesture's definition and settings: itsgiving's reactions, Google's signs, and your own.
#pragma once

#include "gestures/itsgiving.hpp"
#include "gestures/matcher.hpp"

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

}  // namespace racg::gestures
