// Output as our own camera, "Rely-A-Cam Gestures", for Zoom, Discord, Meet and browsers (Windows 11).
// The app registers the camera for this session (it disappears when the app quits) and writes
// frames into the block the camera DLL makes while something is watching it. The DLL is installed
// (regsvr32) by the installer; without it the camera can't start, and the outputs chip says so.
#pragma once

#include "output/pipeline.hpp"

#include <mutex>

struct SwsContext;
struct IMFVirtualCamera;

namespace racg::output {

class WinCameraSink : public Sink {
 public:
  static constexpr const char *kName = "Rely-A-Cam Gestures";
  WinCameraSink();
  ~WinCameraSink() override;
  void send(const QImage &frame, qint64 timeMs) override;
  QString error() const override;

 private:
  bool openBlock();
  mutable std::mutex mu_;
  IMFVirtualCamera *camera_ = nullptr;
  void *handle_ = nullptr;
  uint8_t *view_ = nullptr;
  qint64 retryAt_ = 0;
  SwsContext *sws_ = nullptr;
  int swsW_ = 0, swsH_ = 0, swsOutW_ = 0, swsOutH_ = 0;
  QString error_;
};

}  // namespace racg::output
