// Webcams through Media Foundation (and DirectShow-only virtual cameras like OBS Virtual Camera
// through FFmpeg's dshow device): list them, open one, and get RGB32 frames on a capture thread.
// The webcam's own zoom, pan and tilt (IAMCameraControl on the media source) when it has them, so
// zoom actions use the sensor before falling back to a crop.
#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace racg::vision {

struct CameraInfo {
  QString name;   // "Logitech C920"
  QString id;     // the device's symbolic link
};

class Camera {
 public:
  static QList<CameraInfo> list();

  Camera();
  ~Camera();
  Camera(const Camera &) = delete;
  Camera &operator=(const Camera &) = delete;

  // Opens the camera at the size closest to `want` (at least 25 fps when it can). Blocking (well
  // under a second); frames start arriving on onFrame from the capture thread. onFrame and onLost
  // are read here, so set them before.
  bool open(const QString &id, QSize want = {1280, 720});
  // Never blocks for long: a driver that won't hand back its blocked read (a virtual camera with no
  // picture, like Iriun with the phone away) is left behind after kCloseWaitMs rather than freezing
  // the window or Quit. The thread it's stuck in touches nothing of this object's.
  void close();
  bool isOpen() const;
  QSize size() const { return size_; }
  QString error() const;

  // Called on the capture thread for each frame (Format_RGB32, never mirrored) with a monotonic ms.
  std::function<void(const QImage &, qint64)> onFrame;
  // Called on the capture thread when the camera stops by itself (unplugged, taken by another app).
  std::function<void(const QString &why)> onLost;

  struct Range {
    bool ok = false;
    long min = 0, max = 0, step = 1, def = 0;
  };
  enum Control { Zoom, Pan, Tilt };
  Range range(Control c) const;
  bool set(Control c, long value);
  long get(Control c) const;

  static constexpr int kCloseWaitMs = 1500;

 private:
  struct Impl;
  // Everything the capture thread touches after open() returns, owned jointly by the thread, so a
  // thread left behind by close() never reaches into a Camera that's gone.
  struct Session;
  static void run(Session &s);
  static void runDshow(Session &s);
  bool openDshow(const QString &name);
  std::shared_ptr<Session> session_;
  std::thread thread_;
  QSize size_;
  mutable std::mutex mu_;
  QString error_;
};

}  // namespace racg::vision
