// A meme's pictures over time: a still, a GIF's frames, or a video decoded while it plays (FFmpeg,
// transparency kept: VP8/VP9 alpha through libvpx, ProRes 4444, PNG/QTRLE MOV). Every frame comes
// out as Format_ARGB32_Premultiplied, at most kMaxSide on its long side.
#pragma once

#include "memes/library.hpp"

#include <QImage>
#include <QString>

#include <memory>

namespace racg::memes {

constexpr int kMaxSide = 720;

class Media {
 public:
  // Null (and `error`) when the file can't be played.
  static std::unique_ptr<Media> open(const QString &file, Kind kind, QString *error = nullptr);
  virtual ~Media() = default;
  // The frame `t` seconds after it started; GIFs and videos loop. Any one thread.
  virtual QImage frameAt(double t) = 0;
  virtual QSize size() const = 0;
};

}  // namespace racg::memes
