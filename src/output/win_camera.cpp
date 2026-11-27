#include "output/win_camera.hpp"

#include "vcam/shared.hpp"

#include <QDebug>

#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
// Last: ks.h redefines GUID_NULL as a macro that breaks COM headers included after it.
#include <ks.h>
#include <ksmedia.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace racg::output {

namespace {
using CreateFn = HRESULT(WINAPI *)(MFVirtualCameraType, MFVirtualCameraLifetime, MFVirtualCameraAccess, LPCWSTR, LPCWSTR,
                                   const GUID *, ULONG, IMFVirtualCamera **);
}

WinCameraSink::WinCameraSink() {
  // Windows 11 only: looked up at run time so the app still starts on Windows 10.
  HMODULE lib = LoadLibraryW(L"mfsensorgroup.dll");
  auto create = lib ? reinterpret_cast<CreateFn>(GetProcAddress(lib, "MFCreateVirtualCamera")) : nullptr;
  if (!create) {
    error_ = "needs Windows 11";
    return;
  }
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
  const GUID category = KSCATEGORY_VIDEO_CAMERA;
  HRESULT hr = create(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session, MFVirtualCameraAccess_CurrentUser,
                      vcam::kFriendlyName, vcam::kClsidString, &category, 1, &camera_);
  if (SUCCEEDED(hr)) hr = camera_->Start(nullptr);
  if (FAILED(hr)) {
    qWarning() << "virtual camera: start failed" << Qt::hex << uint32_t(hr);
    if (camera_) {
      camera_->Release();
      camera_ = nullptr;
    }
    error_ = hr == REGDB_E_CLASSNOTREG || hr == HRESULT(0x80070002) ? "not installed: run the installer" : "couldn't start";
    return;
  }
  qInfo() << "virtual camera registered";
}

WinCameraSink::~WinCameraSink() {
  std::lock_guard l(mu_);
  if (view_) UnmapViewOfFile(view_);
  if (handle_) CloseHandle(handle_);
  if (camera_) {
    camera_->Remove();   // a session camera goes away with us anyway; this is immediate
    camera_->Release();
  }
  sws_freeContext(sws_);
}

QString WinCameraSink::error() const {
  std::lock_guard l(mu_);
  return error_;
}

bool WinCameraSink::openBlock() {
  // The camera DLL makes the block when an app starts watching; until then there's nobody to send to.
  handle_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, vcam::kBlockName);
  if (!handle_) return false;
  view_ = static_cast<uint8_t *>(MapViewOfFile(handle_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, vcam::kBlockBytes));
  if (!view_ || memcmp(view_, "RACGVCM1", 8) != 0) {
    if (view_) UnmapViewOfFile(view_);
    CloseHandle(handle_);
    view_ = nullptr;
    handle_ = nullptr;
    return false;
  }
  return true;
}

void WinCameraSink::send(const QImage &frame, qint64 timeMs) {
  std::lock_guard l(mu_);
  if (!camera_) return;
  if (!view_) {
    if (timeMs < retryAt_) return;
    retryAt_ = timeMs + 1000;
    if (!openBlock()) return;
  }
  auto *hd = reinterpret_cast<vcam::Header *>(view_);
  const int ow = hd->wantW & ~1, oh = hd->wantH & ~1;
  if (ow <= 0 || oh <= 0 || uint32_t(ow) > vcam::kMaxW || uint32_t(oh) > vcam::kMaxH) {
    // Nobody watching: let go of the block so the DLL can close it, look again in a second.
    if (timeMs >= retryAt_) {
      UnmapViewOfFile(view_);
      CloseHandle(handle_);
      view_ = nullptr;
      handle_ = nullptr;
      retryAt_ = timeMs + 1000;
    }
    return;
  }
  // Scaled to what the watching app asked for, keeping the picture's shape (bars if it differs).
  if (!sws_ || swsW_ != frame.width() || swsH_ != frame.height() || swsOutW_ != ow || swsOutH_ != oh) {
    sws_freeContext(sws_);
    sws_ = sws_getContext(frame.width(), frame.height(), AV_PIX_FMT_BGRA, ow, oh, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr,
                          nullptr, nullptr);
    swsW_ = frame.width();
    swsH_ = frame.height();
    swsOutW_ = ow;
    swsOutH_ = oh;
  }
  if (!sws_) return;
  uint8_t *y = view_ + vcam::kHeaderBytes;
  uint8_t *dst[4] = {y, y + size_t(ow) * oh, nullptr, nullptr};
  int dstLines[4] = {ow, ow, 0, 0};
  const uint8_t *src[4] = {frame.constBits(), nullptr, nullptr, nullptr};
  int srcLines[4] = {int(frame.bytesPerLine()), 0, 0, 0};
  InterlockedIncrement64(&hd->seq);   // odd: writing
  MemoryBarrier();
  sws_scale(sws_, src, srcLines, 0, frame.height(), dst, dstLines);
  hd->w = ow;
  hd->h = oh;
  hd->writtenMs = int64_t(GetTickCount64());
  MemoryBarrier();
  InterlockedIncrement64(&hd->seq);   // even: whole
}

}  // namespace racg::output
