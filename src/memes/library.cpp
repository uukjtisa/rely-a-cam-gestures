#include "memes/library.hpp"

#include "common/paths.hpp"
#include "gestures/itsgiving.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace racg::memes {

namespace {

constexpr int kVersion = 1;
const QStringList kPicture = {"png", "jpg", "jpeg", "webp", "bmp"};
const QStringList kGif = {"gif"};
const QStringList kVideo = {"mp4", "webm", "mov", "mkv", "m4v"};
const QStringList kSound = {"mp3", "wav", "ogg", "m4a", "aac", "flac", "opus"};

QString kindId(Kind k) { return k == Kind::Gif ? "gif" : k == Kind::Video ? "video" : "picture"; }
Kind kindFrom(const QString &s) { return s == "gif" ? Kind::Gif : s == "video" ? Kind::Video : Kind::Picture; }
QString placeId(Place p) { return p == Place::Full ? "full" : p == Place::Spot ? "spot" : "head"; }
Place placeFrom(const QString &s) { return s == "full" ? Place::Full : s == "spot" ? Place::Spot : Place::Head; }

// A free file name in the media folder, keeping the original name when it can.
QString freeName(const QString &name) {
  QDir dir(paths::mediaDir());
  QFileInfo fi(name);
  QString n = fi.fileName();
  for (int i = 2; dir.exists(n); ++i) n = QString("%1 (%2).%3").arg(fi.completeBaseName()).arg(i).arg(fi.suffix());
  return n;
}

}  // namespace

QString Meme::kindLabel() const {
  const QString ext = QFileInfo(file).suffix().toUpper();
  if (kind == Kind::Gif) return "GIF";
  if (kind == Kind::Video) return alpha ? ext + ", alpha" : ext;
  return ext == "JPEG" ? "JPG" : ext;
}

Meme *Library::find(const QString &id) {
  for (auto &m : memes)
    if (m.id == id) return &m;
  return nullptr;
}
const Meme *Library::find(const QString &id) const { return const_cast<Library *>(this)->find(id); }

bool Library::playable(const QString &path) {
  const QString e = QFileInfo(path).suffix().toLower();
  return kPicture.contains(e) || kGif.contains(e) || kVideo.contains(e);
}

QStringList Library::fileFilters() {
  QStringList all;
  for (const auto &l : {kPicture, kGif, kVideo})
    for (const auto &e : l) all << "*." + e;
  return {"Pictures, GIFs and videos (" + all.join(' ') + ")"};
}

Meme *Library::add(const QString &path, QString id) {
  if (!playable(path)) return nullptr;
  const QString name = freeName(path);
  if (!QFile::copy(path, QDir(paths::mediaDir()).filePath(name))) return nullptr;
  if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
  while (find(id)) id += "_";
  Meme m;
  m.id = id;
  m.file = name;
  const QString e = QFileInfo(path).suffix().toLower();
  m.kind = kGif.contains(e) ? Kind::Gif : kVideo.contains(e) ? Kind::Video : Kind::Picture;
  m.alpha = e == "png" || e == "gif" || e == "webp";   // videos are probed by the player
  memes.push_back(m);
  return &memes.back();
}

bool Library::attachSound(const QString &memeId, const QString &path) {
  Meme *m = find(memeId);
  if (!m || !kSound.contains(QFileInfo(path).suffix().toLower())) return false;
  const QString name = freeName(path);
  if (!QFile::copy(path, QDir(paths::mediaDir()).filePath(name))) return false;
  if (!m->sound.isEmpty()) QFile::remove(QDir(paths::mediaDir()).filePath(m->sound));
  m->sound = name;
  return true;
}

void Library::remove(const QString &id) {
  const Meme *m = find(id);
  if (!m) return;
  QDir dir(paths::mediaDir());
  dir.remove(m->file);
  if (!m->sound.isEmpty()) dir.remove(m->sound);
  std::erase_if(memes, [&](const Meme &x) { return x.id == id; });
}

QStringList Library::importItsgiving(const QString &folder) {
  QDir root(folder);
  QDir assets(root.filePath("assets"));
  if (!assets.exists()) assets = root;   // he may pick the assets folder itself
  QStringList done;
  const QStringList files = assets.entryList(QDir::Files, QDir::Name);
  for (int r = 0; r < gestures::kReactionCount; ++r) {
    const QString pose = gestures::reactionId(gestures::Reaction(r));
    for (const QString &f : files) {
      // its_giving_v2.find_asset_file: the stem is the pose, or ends with "_" + pose.
      const QFileInfo fi(f);
      const QString ext = fi.suffix().toLower();
      if (!(ext == "gif" || ext == "png" || ext == "jpg" || ext == "jpeg")) continue;
      const QString stem = fi.completeBaseName();
      if (stem != pose && !stem.endsWith("_" + pose)) continue;
      if (find(pose)) remove(pose);   // a fresh import replaces the earlier one
      if (add(assets.filePath(f), pose)) done << pose;
      break;
    }
  }
  return done;
}

QJsonObject Library::toJson() const {
  QJsonArray a;
  for (const auto &m : memes)
    a.append(QJsonObject{{"id", m.id},
                         {"file", m.file},
                         {"kind", kindId(m.kind)},
                         {"place", placeId(m.place)},
                         {"spot", QJsonArray{m.spot.x(), m.spot.y(), m.spot.width(), m.spot.height()}},
                         {"sound", m.sound},
                         {"clip_audio", m.clipAudio},
                         {"alpha", m.alpha},
                         {"volume", m.volume}});
  return {{"version", kVersion}, {"memes", a}};
}

Library Library::fromJson(const QJsonObject &o) {
  Library l;
  if (o["version"].toInt() != kVersion) return l;
  for (const auto &v : o["memes"].toArray()) {
    const QJsonObject j = v.toObject();
    Meme m;
    m.id = j["id"].toString();
    m.file = j["file"].toString();
    if (m.id.isEmpty() || m.file.isEmpty()) continue;
    m.kind = kindFrom(j["kind"].toString());
    m.place = placeFrom(j["place"].toString());
    const QJsonArray s = j["spot"].toArray();
    if (s.size() == 4) m.spot = QRectF(s[0].toDouble(), s[1].toDouble(), s[2].toDouble(), s[3].toDouble());
    m.sound = j["sound"].toString();
    m.clipAudio = j["clip_audio"].toBool();
    m.alpha = j["alpha"].toBool();
    m.volume = std::clamp(j["volume"].toInt(80), 0, 100);
    l.memes.push_back(m);
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
  if (!f.open(QIODevice::ReadOnly)) return {};
  return fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

}  // namespace racg::memes
