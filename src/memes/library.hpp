// The memes: pictures, GIFs and videos (with their transparency), each with where it goes and its
// sound. Their files are copied into the library's media folder, so the library is self-contained.
#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <vector>

namespace racg::memes {

enum class Kind { Picture, Gif, Video };
enum class Place { Head, Full, Spot };

struct Meme {
  QString id;             // stable; itsgiving's imports use its pose name ("heart")
  QString file;           // its file in the media folder
  Kind kind = Kind::Picture;
  Place place = Place::Head;
  QRectF spot{0.70, 0.06, 0.24, 0.30};   // the fixed spot, as fractions of the frame
  QString sound;          // an attached sound file in the media folder; empty = the clip's own audio
  bool clipAudio = false; // the video file has its own sound (found when added)
  bool alpha = false;     // the file has transparency (found when added)
  int volume = 80;        // 0..100

  QString name() const { return file; }
  bool hasSound() const { return !sound.isEmpty() || clipAudio; }
  QString kindLabel() const;   // "GIF", "WebM, alpha", "PNG"
};

class Library {
 public:
  std::vector<Meme> memes;

  Meme *find(const QString &id);
  const Meme *find(const QString &id) const;
  // Copies a file into the media folder and adds it (its kind from the extension). Null if the
  // file type isn't one we play. `id` is made unique; pass empty for a fresh one.
  Meme *add(const QString &path, QString id = {});
  bool attachSound(const QString &memeId, const QString &path);
  void remove(const QString &id);   // also deletes its copies in the media folder

  // itsgiving's assets folder: each picture whose name is (or ends with _) a pose name is copied
  // in under that pose's id. Returns the pose ids imported.
  QStringList importItsgiving(const QString &folder);

  static bool playable(const QString &path);
  static QStringList fileFilters();   // for the open dialog

  QJsonObject toJson() const;
  static Library fromJson(const QJsonObject &o);
  bool save(const QString &path) const;
  static Library load(const QString &path);
};

}  // namespace racg::memes
