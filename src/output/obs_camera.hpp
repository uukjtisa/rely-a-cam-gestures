// Output into OBS Virtual Camera without OBS: OBS's camera reads its frames from one shared memory
// queue ("OBSVirtualCamVideo", NV12, three frames), the same one OBS writes when you press Start
// Virtual Camera. Only one program can write it: while OBS's own virtual camera is on, this one
// waits (and says so), and OBS can't start its own while this one is writing.
// The layout follows OBS's plugins/win-dshow/shared-memory-queue.c (GPL-2.0-or-later).
#pragma once

#include "output/pipeline.hpp"

#include <mutex>

struct SwsContext;

namespace racg::output {

class ObsCameraSink : public Sink {
 public:
  ObsCameraSink();
  ~ObsCameraSink() override;
  void send(const QImage &frame, qint64 timeMs) override;
  QString error() const override;

 private:
  bool open(int w, int h);
  void close();
  mutable std::mutex mu_;
  void *handle_ = nullptr;
  uint8_t *view_ = nullptr;
  uint32_t offsets_[3] = {};
  int w_ = 0, h_ = 0;
  qint64 retryAt_ = 0, startMs_ = -1;
  SwsContext *sws_ = nullptr;
  QString error_;
};

}  // namespace racg::output
