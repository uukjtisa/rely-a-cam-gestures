// The memes showing on one picture (the webcam, or one phone) and where: on the head (itsgiving's
// way: a square centred on the face, twice its height, smoothed; one head slot, the newest wins),
// the full frame, or its fixed spot. Started and stopped on the UI thread, drawn on whichever
// thread composes the picture.
#pragma once

#include "memes/library.hpp"
#include "memes/media.hpp"
#include "output/overlay.hpp"
#include "output/pipeline.hpp"

#include <QPainter>
#include <QString>

#include <map>
#include <memory>
#include <mutex>

namespace racg::memes {

class Show {
 public:
  // Returns an error to show when the file can't be played.
  QString start(const Meme &m, qint64 tMs);
  void stop(const QString &memeId);
  void stopAll();
  bool any() const;
  // The library changed (a spot was dragged, a placement picked): showing memes follow it.
  void refresh(const Library &lib);
  void draw(QPainter &p, const output::FrameInfo &info, const output::DetectionState &det, qint64 tMs);

 private:
  struct Playing {
    Meme meme;
    std::shared_ptr<Media> media;
    qint64 since = 0;
  };
  mutable std::mutex mu_;
  std::map<QString, Playing> playing_;
  QString headOwner_;          // the meme in the head slot
  QRectF head_;                // the smoothed head box, output pixels
  qint64 headAt_ = -1, faceSeen_ = -1;
};

}  // namespace racg::memes
