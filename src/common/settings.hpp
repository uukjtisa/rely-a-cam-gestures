// The app's settings (the Settings dialog, the overlays menu, the camera picked), one JSON file.
#pragma once

#include "gestures/matcher.hpp"
#include "vision/observation.hpp"

#include <QString>

namespace racg {

struct Settings {
  QString cameraId;              // the webcam's symbolic link; empty = the first one
  bool gesturesOn = true;
  bool preferGpu = true;

  // Outputs on its own (the installer ticks both; Settings can untick either).
  bool outVirtualCamera = true;  // our Windows 11 camera
  bool outObsCamera = true;      // OBS Virtual Camera's shared memory
  enum class MemeSound { Speakers, Calls } memeSound = MemeSound::Speakers;
  QString callMic;               // the real mic mixed into calls; empty = Windows' default
  QString cameraOffPicture;      // shown while the camera is off; empty = a dark frame

  vision::Sign confirmSign = vision::Sign::ThumbUp;
  gestures::StopSign stopSign = gestures::StopSign::BothPalms;
  bool startWithWindows = false;
  bool itsgivingFetched = false; // itsgiving's 14 pictures were downloaded once (retried until then)

  struct Places {
    bool preview = false, output = false;
  };
  Places grid{false, false}, detection{true, false}, faceMap{false, false};
  double followSpan = 1.8;      // follow face: face heights per picture height (smaller = closer)

  int obsPort = 4455;
  QString obsPassword;           // kept encrypted for this Windows user (DPAPI) in the file

  static Settings load(const QString &path);
  bool save(const QString &path) const;
};

}  // namespace racg
