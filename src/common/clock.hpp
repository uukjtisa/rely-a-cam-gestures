// One monotonic clock for the whole app: camera frames, the detector, the engine and the window all
// stamp time with it, so "held for 0.3 s" means the same thing everywhere.
#pragma once

#include <QElapsedTimer>

namespace racg::clock {

inline qint64 nowMs() {
  static const QElapsedTimer t = [] {
    QElapsedTimer e;
    e.start();
    return e;
  }();
  return t.elapsed();
}

inline double now() { return nowMs() / 1000.0; }

}  // namespace racg::clock
