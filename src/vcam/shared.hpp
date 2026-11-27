// What the app and the "Rely-A-Cam Gestures" camera share. The camera is a media source DLL that
// Windows 11's Frame Server loads in its own service (session 0) whenever an app opens the camera;
// the DLL makes a Global\ block, says which size it wants, and the app writes NV12 frames into it.
// No Qt here: the DLL links nothing but Windows.
#pragma once

#include <cstdint>

namespace racg::vcam {

// {6B1C2F0E-6A43-4C1B-9E7D-3A5B8C0D2E71}
constexpr wchar_t kClsidString[] = L"{6B1C2F0E-6A43-4C1B-9E7D-3A5B8C0D2E71}";
constexpr wchar_t kFriendlyName[] = L"Rely-A-Cam Gestures";
constexpr wchar_t kBlockName[] = L"Global\\RelyACamGesturesCamera";

constexpr uint32_t kMaxW = 1920, kMaxH = 1080;
constexpr uint32_t kFrameBytes = kMaxW * kMaxH * 3 / 2;
constexpr uint32_t kHeaderBytes = 64;
constexpr uint32_t kBlockBytes = kHeaderBytes + kFrameBytes;

struct Header {
  char magic[8];               // "RACGVCM1"
  volatile int32_t wantW;      // the size an app is watching at; 0 = nobody is watching
  volatile int32_t wantH;
  volatile int64_t seq;        // odd while the app writes, even when a frame is whole
  volatile int32_t w, h;       // the frame's size (equals want once the app follows it)
  volatile int64_t writtenMs;  // GetTickCount64 when the frame was finished
};
static_assert(sizeof(Header) <= kHeaderBytes);

}  // namespace racg::vcam
