#include "output/obs_camera.hpp"

#include <windows.h>
#include <tlhelp32.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace racg::output {

namespace {

const wchar_t *kName = L"OBSVirtualCamVideo";
enum State : uint32_t { Invalid = 0, Starting = 1, Ready = 2, Stopping = 3 };
constexpr uint32_t kFrameHeader = 32;   // a frame's timestamp, padded

struct QueueHeader {
  volatile uint32_t writeIdx;
  volatile uint32_t readIdx;
  volatile uint32_t state;
  uint32_t offsets[3];
  uint32_t type;
  uint32_t cx;
  uint32_t cy;
  uint64_t interval;   // 100 ns units
  uint32_t reserved[8];
};

uint32_t align32(uint32_t v) { return (v + 31) & ~31u; }

// OBS makes the queue itself when its Start Virtual Camera is pressed, and refuses if the name
// already exists. So while OBS is open we never hold it: OBS's own button must always work.
bool obsRunning() {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W e{sizeof(e)};
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
    found = _wcsicmp(e.szExeFile, L"obs64.exe") == 0;
  CloseHandle(snap);
  return found;
}

}  // namespace

ObsCameraSink::ObsCameraSink() = default;
ObsCameraSink::~ObsCameraSink() {
  std::lock_guard l(mu_);
  close();
  sws_freeContext(sws_);
}

QString ObsCameraSink::error() const {
  std::lock_guard l(mu_);
  return error_;
}

bool ObsCameraSink::open(int w, int h) {
  close();
  // Fail if someone (OBS) is already writing it.
  if (HANDLE h0 = OpenFileMappingW(FILE_MAP_READ, FALSE, kName)) {
    CloseHandle(h0);
    error_ = "OBS is using it";
    return false;
  }
  const uint32_t frame = uint32_t(w) * uint32_t(h) * 3 / 2;
  uint32_t size = align32(sizeof(QueueHeader));
  for (auto &o : offsets_) {
    o = size;
    size = align32(size + frame + kFrameHeader);
  }
  handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, kName);
  if (!handle_) {
    error_ = "can't be opened";
    return false;
  }
  view_ = static_cast<uint8_t *>(MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
  if (!view_) {
    CloseHandle(handle_);
    handle_ = nullptr;
    error_ = "can't be opened";
    return false;
  }
  QueueHeader hd = {};
  hd.state = Starting;
  hd.cx = uint32_t(w);
  hd.cy = uint32_t(h);
  hd.interval = 333333;   // 30 fps
  for (int i = 0; i < 3; ++i) hd.offsets[i] = offsets_[i];
  memcpy(view_, &hd, sizeof(hd));
  w_ = w;
  h_ = h;
  error_.clear();
  return true;
}

void ObsCameraSink::close() {
  if (view_) {
    reinterpret_cast<QueueHeader *>(view_)->state = Stopping;
    UnmapViewOfFile(view_);
    view_ = nullptr;
  }
  if (handle_) {
    CloseHandle(handle_);
    handle_ = nullptr;
  }
  w_ = h_ = 0;
}

void ObsCameraSink::send(const QImage &frame, qint64 timeMs) {
  std::lock_guard l(mu_);
  const int w = frame.width() & ~1, h = frame.height() & ~1;
  if (w < 2 || h < 2) return;
  if (timeMs >= obsCheckAt_) {
    obsCheckAt_ = timeMs + 2000;
    obsOpen_ = obsRunning();
  }
  if (obsOpen_) {
    close();
    error_ = "OBS is open, so OBS Virtual Camera stays OBS's own";
    return;
  }
  if (!view_ || w != w_ || h != h_) {
    if (timeMs < retryAt_) return;
    if (!open(w, h)) {
      retryAt_ = timeMs + 2000;   // OBS may turn its own off later
      return;
    }
  }
  sws_ = sws_getCachedContext(sws_, w, h, AV_PIX_FMT_BGRA, w, h, AV_PIX_FMT_NV12, SWS_POINT, nullptr, nullptr, nullptr);
  if (!sws_) return;
  auto *hd = reinterpret_cast<QueueHeader *>(view_);
  const uint32_t inc = uint32_t(InterlockedIncrement(reinterpret_cast<volatile LONG *>(&hd->writeIdx)));
  const uint32_t idx = inc % 3;
  uint8_t *base = view_ + offsets_[idx];
  if (startMs_ < 0) startMs_ = timeMs;
  *reinterpret_cast<uint64_t *>(base) = uint64_t(timeMs - startMs_) * 1000000ull;   // ns, like OBS
  uint8_t *dst[4] = {base + kFrameHeader, base + kFrameHeader + size_t(w) * h, nullptr, nullptr};
  int dstLines[4] = {w, w, 0, 0};
  const uint8_t *src[4] = {frame.constBits(), nullptr, nullptr, nullptr};
  int srcLines[4] = {int(frame.bytesPerLine()), 0, 0, 0};
  sws_scale(sws_, src, srcLines, 0, h, dst, dstLines);
  hd->readIdx = inc;
  hd->state = Ready;
}

}  // namespace racg::output
