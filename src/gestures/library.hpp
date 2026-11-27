// The gestures: itsgiving's 14 reactions and Google's 7 signs (fixed, with your settings on them)
// and your own (recorded). Saved as JSON in the library folder, so it's easy to back up and the
// phone can browse it.
#pragma once

#include "gestures/controls.hpp"
#include "gestures/gesture.hpp"

#include <QJsonObject>
#include <QString>

#include <vector>

namespace racg::gestures {

class Library {
 public:
  std::vector<GestureDef> gestures;
  Controls controls;   // projected onto the gestures by applyControls()
  // Zoom on what I hold: examples of a hand holding something, recorded like a gesture's takes.
  std::vector<Take> holdTakes;

  static Library defaults();
  GestureDef *find(const QString &id);
  const GestureDef *find(const QString &id) const;
  GestureDef &addMine(const QString &name);
  void remove(const QString &id);
  void applyControls() { controls.apply(gestures); }

  QJsonObject toJson() const;
  static Library fromJson(const QJsonObject &o);   // defaults with the saved settings laid over them
  bool save(const QString &path) const;
  static Library load(const QString &path);
};

}  // namespace racg::gestures
