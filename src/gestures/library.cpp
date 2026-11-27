#include "gestures/library.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace racg::gestures {

namespace {

constexpr int kVersion = 1;

const char *kReactionIcon[kReactionCount] = {"⏱", "🫶", "🤭", "😱", "💃", "🤏", "😘",
                                             "🙋", "😛", "😮", "🤢", "🗣", "🤨", "🌀"};
const char *kSignIcon[] = {"", "✊", "✋", "☝", "👎", "👍", "✌", "🤟"};
// Google's signs in the order the window lists them.
constexpr vision::Sign kSignOrder[] = {vision::Sign::ThumbUp, vision::Sign::ThumbDown, vision::Sign::OpenPalm,
                                       vision::Sign::ClosedFist, vision::Sign::Victory, vision::Sign::PointingUp,
                                       vision::Sign::ILoveYou};

QString endsId(Ends e) { return e == Ends::Held ? "held" : e == Ends::Again ? "again" : "after"; }
Ends endsFrom(const QString &s) { return s == "again" ? Ends::Again : s == "after" ? Ends::After : Ends::Held; }

}  // namespace

QString GestureDef::how() const {
  switch (kind) {
    case Kind::Ready: return reactionHow(reaction());
    case Kind::Sign: return "Google's hand sign";
    case Kind::Mine:
      return takes.empty() ? QString("your own, no takes yet")
                           : QString("your own, %1 take%2").arg(takes.size()).arg(takes.size() == 1 ? "" : "s");
  }
  return {};
}

bool GestureDef::usesBothHands() const {
  if (kind == Kind::Mine) return parts.bothHands();
  if (kind == Kind::Ready) {
    switch (reaction()) {
      case Reaction::TimeOut: case Reaction::Heart: case Reaction::CoverNose: case Reaction::CrashingOut:
      case Reaction::Dance: return true;
      default: return false;
    }
  }
  return false;
}

Library Library::defaults() {
  Library l;
  for (int i = 0; i < kReactionCount; ++i) {
    GestureDef g;
    g.kind = Kind::Ready;
    g.ref = i;
    g.id = QString("ready:") + reactionId(Reaction(i));
    g.name = reactionName(Reaction(i));
    g.icon = kReactionIcon[i];
    g.hold = reactionArmSeconds(Reaction(i));
    // What the rule watches, shown on its (disabled) part ticks.
    const auto r = Reaction(i);
    const bool hands = r <= Reaction::HandUp || r == Reaction::TalkingToWall;
    g.parts = {hands, hands, r != Reaction::TimeOut && r != Reaction::Heart && r != Reaction::TalkingToWall,
               r == Reaction::Dance || r == Reaction::CrashingOut};
    l.gestures.push_back(g);
  }
  for (vision::Sign s : kSignOrder) {
    GestureDef g;
    g.kind = Kind::Sign;
    g.ref = int(s);
    g.id = QString("sign:") + vision::signId(s);
    g.name = vision::signLabel(s);
    g.icon = kSignIcon[int(s)];
    g.hold = 0.3f;
    g.on = s == vision::Sign::ThumbUp;
    g.parts = {true, true, false, false};
    l.gestures.push_back(g);
  }
  return l;
}

GestureDef *Library::find(const QString &id) {
  for (auto &g : gestures)
    if (g.id == id) return &g;
  return nullptr;
}
const GestureDef *Library::find(const QString &id) const { return const_cast<Library *>(this)->find(id); }

GestureDef &Library::addMine(const QString &name) {
  GestureDef g;
  g.kind = Kind::Mine;
  g.id = "mine:" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  g.name = name;
  g.icon = "✨";
  g.hold = 0.3f;
  gestures.push_back(g);
  return gestures.back();
}

void Library::remove(const QString &id) {
  std::erase_if(gestures, [&](const GestureDef &g) { return g.id == id && g.kind == Kind::Mine; });
}

QJsonObject Library::toJson() const {
  QJsonArray a;
  for (const auto &g : gestures) {
    QJsonObject o{{"id", g.id},          {"on", g.on},       {"action", g.action}, {"ends", endsId(g.ends)},
                  {"secs", g.secs},      {"stop", g.stop},   {"confirm", g.confirm}, {"sensitivity", g.sens},
                  {"hold", g.hold}};
    if (g.kind == Kind::Mine) {
      o["name"] = g.name;
      o["icon"] = g.icon;
      o["parts"] = g.parts.toJson();
      QJsonArray t;
      for (const auto &take : g.takes) t.append(take.toJson());
      o["takes"] = t;
    }
    a.append(o);
  }
  return {{"version", kVersion}, {"gestures", a}};
}

Library Library::fromJson(const QJsonObject &root) {
  Library l = defaults();
  if (root["version"].toInt() != kVersion) return l;
  for (const auto &v : root["gestures"].toArray()) {
    const QJsonObject o = v.toObject();
    const QString id = o["id"].toString();
    GestureDef *g = l.find(id);
    if (!g) {
      if (!id.startsWith("mine:")) continue;   // a ready-made one this version doesn't know
      GestureDef m;
      m.kind = Kind::Mine;
      m.id = id;
      m.name = o["name"].toString("Gesture");
      m.icon = o["icon"].toString("✨");
      m.parts = Parts::fromJson(o["parts"].toObject());
      for (const auto &t : o["takes"].toArray()) m.takes.push_back(Take::fromJson(t.toObject()));
      l.gestures.push_back(m);
      g = &l.gestures.back();
    }
    g->on = o["on"].toBool(g->on);
    g->action = o["action"].toString();
    g->ends = endsFrom(o["ends"].toString());
    g->secs = std::clamp(o["secs"].toInt(3), 1, 600);
    g->stop = o["stop"].toBool(true);
    g->confirm = o["confirm"].toBool(false);
    g->sens = std::clamp(o["sensitivity"].toInt(kSensDefault), kSensMin, kSensMax);
    g->hold = std::clamp(float(o["hold"].toDouble(g->hold)), 0.03f, 5.f);
  }
  return l;
}

bool Library::save(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
  return f.commit();
}

Library Library::load(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return defaults();
  return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

}  // namespace racg::gestures
