// Where things live. Everything of yours is under %APPDATA%\Rely-A-Cam-Gestures, and the library
// (gestures, memes and their media) is one folder in it, so it's easy to back up.
#pragma once

#include <QString>

namespace racg::paths {

constexpr const char *kAppName = "Rely-A-Cam-Gestures";

QString dataDir();          // %APPDATA%\Rely-A-Cam-Gestures (made if missing)
QString libraryDir();       // <data>\library
QString mediaDir();         // <library>\media: meme pictures, GIFs, videos, sounds
QString takesDir();         // <library>\takes: a picture per recorded take
QString gesturesFile();     // <library>\gestures.json
QString memesFile();        // <library>\memes.json
QString calibrationFile();  // <data>\calibration.json (itsgiving's format)
QString settingsFile();     // <data>\settings.json
QString logFile();          // <data>\log.txt
QString snapshotsDir();     // Pictures\Rely-A-Cam-Gestures
QString modelsDir();        // <exe>\models, or the source tree's models\ in a developer build

}  // namespace racg::paths
