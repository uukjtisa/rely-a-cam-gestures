// Meme sound. A clip's own audio or an attached file is decoded once to 48 kHz stereo float; the
// mixer plays any number of them and sends the mix one of three ways:
//   Speakers: Windows' default output.
//   Calls:    your real mic plus the memes, into VB-Audio's "CABLE Input", so Discord/Zoom pick
//             "CABLE Output" as their mic (the cable is installed from Rely-A-Cam's dock, or by hand).
//   Link:     to Rely-A-Cam's plugin, which plays it through the Phone Memes source in OBS.
// The writer and the endpoint lookup follow Rely-A-Cam's obs-plugin/src/audio/cable.cpp
// (GPL-2.0-or-later, same author).
#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace racg::audio {

constexpr int kRate = 48000;

struct Clip {
  std::vector<float> lr;   // interleaved stereo at kRate
  int frames() const { return int(lr.size() / 2); }
};

// The first audio stream of a file, at most 60 s. Null when it has none.
std::shared_ptr<const Clip> load(const QString &file);
// Does the file have an audio stream (a video's own sound)?
bool hasAudio(const QString &file);
bool cableInstalled();
QStringList microphones();   // "id|name" for the Settings list

class Mixer {
 public:
  enum class Route { Speakers, Calls, Link };
  Mixer();
  ~Mixer();
  void setRoute(Route r, const QString &micId = {});
  Route route() const { return route_; }
  // Plays a clip under `key` (a channel + meme); playing it again restarts it.
  void play(const QString &key, std::shared_ptr<const Clip> clip, float volume, bool loop);
  void stop(const QString &key);
  void stopAll();
  // Route::Link: 10 ms blocks of interleaved stereo, on the mixer's thread.
  std::function<void(const float *lr, int frames)> onLink;
  QString error() const;

 private:
  struct Voice {
    std::shared_ptr<const Clip> clip;
    float volume = 1;
    bool loop = false;
    int pos = 0;
  };
  void run();
  void mix(float *out, int frames);
  struct Device;
  std::thread thread_;
  std::atomic<bool> quit_{false}, reopen_{true};
  std::atomic<Route> route_{Route::Speakers};
  QString micId_;
  mutable std::mutex mu_;
  std::map<QString, Voice> voices_;
  QString error_;
};

}  // namespace racg::audio
