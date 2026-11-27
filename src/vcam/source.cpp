// The "Rely-A-Cam Gestures" camera: a Media Foundation media source that Windows 11's Frame Server
// loads (through an activator, by CLSID) when an app opens the camera. One video stream, NV12 at
// 1920x1080, 1280x720 or 640x360, 30 fps. While a stream runs it keeps a Global\ block (shared.hpp)
// that the app writes frames into; with no app writing it shows black.
// Shaped after Microsoft's Windows-Camera "VirtualCamera" sample (MIT): the same interfaces and the
// same event order the Frame Server expects.

#include "vcam/shared.hpp"

// Order matters: every GUID is defined here (initguid), and ks.h comes last because it redefines
// GUID_NULL as a macro that breaks any COM header included after it.
#include <initguid.h>
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <sddl.h>
#include <wrl/implements.h>
#include <wrl/module.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>   // IKsControl (user mode)

#include <mutex>
#include <vector>

using namespace Microsoft::WRL;

namespace {

constexpr UINT32 kSizes[][2] = {{1280, 720}, {1920, 1080}, {640, 360}};
constexpr LONGLONG kFrame = 333333;   // 30 fps, 100 ns units

HRESULT makeType(UINT32 w, UINT32 h, IMFMediaType **out) {
  ComPtr<IMFMediaType> t;
  HRESULT hr = MFCreateMediaType(&t);
  if (FAILED(hr)) return hr;
  t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  t->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  MFSetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, w, h);
  MFSetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, 30, 1);
  MFSetAttributeRatio(t.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  t->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  t->SetUINT32(MF_MT_DEFAULT_STRIDE, w);
  t->SetUINT32(MF_MT_SAMPLE_SIZE, w * h * 3 / 2);
  t->SetUINT32(MF_MT_AVG_BITRATE, w * h * 12 * 30);
  *out = t.Detach();
  return S_OK;
}

HRESULT streamAttributes(IMFAttributes *a) {
  a->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE);
  a->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
  a->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
  a->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
  return S_OK;
}

// The block the app writes into: made here (the Frame Server's service may create Global\ names),
// open to the signed-in user's app.
class Block {
 public:
  ~Block() { close(); }
  void open(UINT32 w, UINT32 h) {
    if (!view_) {
      SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, FALSE};
      PSECURITY_DESCRIPTOR sd = nullptr;
      if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)(A;;GRGW;;;LS)",
                                                               SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
      handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, racg::vcam::kBlockBytes, racg::vcam::kBlockName);
      if (sd) LocalFree(sd);
      if (!handle_) return;
      view_ = static_cast<BYTE *>(MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, racg::vcam::kBlockBytes));
      if (!view_) {
        CloseHandle(handle_);
        handle_ = nullptr;
        return;
      }
      memcpy(view_, "RACGVCM1", 8);
    }
    header()->wantW = LONG(w);
    header()->wantH = LONG(h);
  }
  void close() {
    if (view_) {
      header()->wantW = 0;
      header()->wantH = 0;
      UnmapViewOfFile(view_);
      view_ = nullptr;
    }
    if (handle_) {
      CloseHandle(handle_);
      handle_ = nullptr;
    }
  }
  // Copies the newest whole frame of this size; false when there's none (no app, or mid-change).
  bool read(BYTE *dst, UINT32 w, UINT32 h) {
    if (!view_) return false;
    racg::vcam::Header *hd = header();
    const LONG64 s1 = hd->seq;
    MemoryBarrier();
    if ((s1 & 1) || UINT32(hd->w) != w || UINT32(hd->h) != h) return false;
    if (LONG64(GetTickCount64()) - hd->writtenMs > 2000) return false;   // the app stopped
    memcpy(dst, view_ + racg::vcam::kHeaderBytes, size_t(w) * h * 3 / 2);
    MemoryBarrier();
    return hd->seq == s1;
  }

 private:
  racg::vcam::Header *header() { return reinterpret_cast<racg::vcam::Header *>(view_); }
  HANDLE handle_ = nullptr;
  BYTE *view_ = nullptr;
};

class Source;

class Stream : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>,
                                   IKsControl> {
 public:
  HRESULT RuntimeClassInitialize(IMFMediaSource *source) {
    source_ = source;
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (FAILED(hr)) return hr;
    std::vector<IMFMediaType *> types;
    for (const auto &s : kSizes) {
      IMFMediaType *t = nullptr;
      if (SUCCEEDED(makeType(s[0], s[1], &t))) types.push_back(t);
    }
    hr = MFCreateStreamDescriptor(0, DWORD(types.size()), types.data(), &desc_);
    if (SUCCEEDED(hr)) {
      ComPtr<IMFMediaTypeHandler> handler;
      desc_->GetMediaTypeHandler(&handler);
      handler->SetCurrentMediaType(types[0]);
      streamAttributes(desc_.Get());
      MFCreateAttributes(&attrs_, 4);
      streamAttributes(attrs_.Get());
    }
    for (auto *t : types) t->Release();
    return hr;
  }
  IMFStreamDescriptor *descriptor() const { return desc_.Get(); }
  IMFAttributes *attributes() const { return attrs_.Get(); }

  HRESULT start(IMFMediaType *type) {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    UINT32 w = 0, h = 0;
    if (type) MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h);
    if (w && h) {
      w_ = w;
      h_ = h;
    }
    state_ = MF_STREAM_STATE_RUNNING;
    block_.open(w_, h_);
    return queue_->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
  }
  HRESULT stop() {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    state_ = MF_STREAM_STATE_STOPPED;
    block_.close();
    return queue_->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
  }
  void shutdown() {
    std::lock_guard l(mu_);
    block_.close();
    if (queue_) queue_->Shutdown();
    queue_.Reset();
    source_ = nullptr;
  }

  // IMFMediaEventGenerator
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback *cb, IUnknown *state) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->BeginGetEvent(cb, state) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult *r, IMFMediaEvent **e) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent **e) override {
    ComPtr<IMFMediaEventQueue> q;
    {
      std::lock_guard l(mu_);
      q = queue_;
    }
    return q ? q->GetEvent(flags, e) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP QueueEvent(MediaEventType t, REFGUID ext, HRESULT status, const PROPVARIANT *v) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->QueueEventParamVar(t, ext, status, v) : MF_E_SHUTDOWN;
  }

  // IMFMediaStream
  IFACEMETHODIMP GetMediaSource(IMFMediaSource **s) override {
    std::lock_guard l(mu_);
    if (!s) return E_POINTER;
    if (!source_) return MF_E_SHUTDOWN;
    *s = source_;
    (*s)->AddRef();
    return S_OK;
  }
  IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor **d) override {
    std::lock_guard l(mu_);
    if (!d) return E_POINTER;
    if (!queue_) return MF_E_SHUTDOWN;
    return desc_.CopyTo(d);
  }
  IFACEMETHODIMP RequestSample(IUnknown *token) override {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    if (state_ != MF_STREAM_STATE_RUNNING) return MF_E_MEDIA_SOURCE_WRONGSTATE;
    const DWORD size = w_ * h_ * 3 / 2;
    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buf;
    HRESULT hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = MFCreateMemoryBuffer(size, &buf);
    if (FAILED(hr)) return hr;
    if (last_.size() != size) {
      last_.assign(size, 0);
      lastValid_ = false;
    }
    BYTE *data = nullptr;
    if (SUCCEEDED(buf->Lock(&data, nullptr, nullptr))) {
      const LONG64 now = LONG64(GetTickCount64());
      if (block_.read(data, w_, h_)) {
        memcpy(last_.data(), data, size);
        lastValid_ = true;
        lastAt_ = now;
      } else if (lastValid_ && now - lastAt_ < 2000) {
        memcpy(data, last_.data(), size);   // a frame torn mid-copy: show the one before
      } else {
        memset(data, 16, size_t(w_) * h_);                          // black: Y = 16
        memset(data + size_t(w_) * h_, 128, size_t(w_) * h_ / 2);   //        UV = 128
        lastValid_ = false;
      }
      buf->Unlock();
    }
    buf->SetCurrentLength(size);
    sample->AddBuffer(buf.Get());
    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(kFrame);
    if (token) sample->SetUnknown(MFSampleExtension_Token, token);
    return queue_->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.Get());
  }

  // IMFMediaStream2
  IFACEMETHODIMP SetStreamState(MF_STREAM_STATE s) override {
    {
      std::lock_guard l(mu_);
      if (s == state_) return S_OK;
    }
    if (s == MF_STREAM_STATE_RUNNING) return start(nullptr);
    if (s == MF_STREAM_STATE_STOPPED) return stop();
    return MF_E_INVALID_STATE_TRANSITION;
  }
  IFACEMETHODIMP GetStreamState(MF_STREAM_STATE *s) override {
    std::lock_guard l(mu_);
    if (!s) return E_POINTER;
    *s = state_;
    return S_OK;
  }

  // IKsControl: no camera controls.
  IFACEMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
  IFACEMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
  IFACEMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }

 private:
  std::mutex mu_;
  IMFMediaSource *source_ = nullptr;   // the source owns us
  ComPtr<IMFMediaEventQueue> queue_;
  ComPtr<IMFStreamDescriptor> desc_;
  ComPtr<IMFAttributes> attrs_;
  MF_STREAM_STATE state_ = MF_STREAM_STATE_STOPPED;
  UINT32 w_ = 1280, h_ = 720;
  Block block_;
  std::vector<BYTE> last_;
  bool lastValid_ = false;
  LONG64 lastAt_ = 0;
};

class Source : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                                   ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>, IMFGetService, IKsControl> {
 public:
  HRESULT init() {
    HRESULT hr = MFCreateEventQueue(&queue_);
    if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attrs_, 2);
    if (SUCCEEDED(hr)) hr = MakeAndInitialize<Stream>(&stream_, static_cast<IMFMediaSource *>(this));
    if (FAILED(hr)) return hr;
    IMFStreamDescriptor *sd = stream_->descriptor();
    hr = MFCreatePresentationDescriptor(1, &sd, &pd_);
    if (SUCCEEDED(hr)) pd_->SelectStream(0);
    return hr;
  }

  // IMFMediaEventGenerator
  IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback *cb, IUnknown *state) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->BeginGetEvent(cb, state) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP EndGetEvent(IMFAsyncResult *r, IMFMediaEvent **e) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->EndGetEvent(r, e) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent **e) override {
    ComPtr<IMFMediaEventQueue> q;
    {
      std::lock_guard l(mu_);
      q = queue_;
    }
    return q ? q->GetEvent(flags, e) : MF_E_SHUTDOWN;
  }
  IFACEMETHODIMP QueueEvent(MediaEventType t, REFGUID ext, HRESULT status, const PROPVARIANT *v) override {
    std::lock_guard l(mu_);
    return queue_ ? queue_->QueueEventParamVar(t, ext, status, v) : MF_E_SHUTDOWN;
  }

  // IMFMediaSource
  IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor **pd) override {
    std::lock_guard l(mu_);
    if (!pd) return E_POINTER;
    if (!queue_) return MF_E_SHUTDOWN;
    return pd_->Clone(pd);
  }
  IFACEMETHODIMP GetCharacteristics(DWORD *c) override {
    std::lock_guard l(mu_);
    if (!c) return E_POINTER;
    if (!queue_) return MF_E_SHUTDOWN;
    *c = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
  }
  IFACEMETHODIMP Start(IMFPresentationDescriptor *pd, const GUID *timeFormat, const PROPVARIANT *startPos) override {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    if (!pd || !startPos) return E_INVALIDARG;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    HRESULT hr = pd->GetStreamDescriptorByIndex(0, &selected, &sd);
    if (FAILED(hr)) return hr;
    if (selected) {
      ComPtr<IMFMediaTypeHandler> handler;
      ComPtr<IMFMediaType> type;
      if (SUCCEEDED(sd->GetMediaTypeHandler(&handler))) handler->GetCurrentMediaType(&type);
      MF_STREAM_STATE st = MF_STREAM_STATE_STOPPED;
      stream_->GetStreamState(&st);
      ComPtr<IUnknown> unk;
      stream_.As(&unk);
      queue_->QueueEventParamUnk(st == MF_STREAM_STATE_STOPPED ? MENewStream : MEUpdatedStream, GUID_NULL, S_OK, unk.Get());
      hr = stream_->start(type.Get());
      if (FAILED(hr)) return hr;
    }
    return queue_->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, startPos);
  }
  IFACEMETHODIMP Stop() override {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    stream_->stop();
    return queue_->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
  }
  IFACEMETHODIMP Pause() override { return MF_E_INVALID_STATE_TRANSITION; }
  IFACEMETHODIMP Shutdown() override {
    std::lock_guard l(mu_);
    if (!queue_) return MF_E_SHUTDOWN;
    if (stream_) stream_->shutdown();
    queue_->Shutdown();
    queue_.Reset();
    stream_.Reset();
    pd_.Reset();
    return S_OK;
  }

  // IMFMediaSourceEx
  IFACEMETHODIMP GetSourceAttributes(IMFAttributes **a) override {
    std::lock_guard l(mu_);
    if (!a) return E_POINTER;
    if (!queue_) return MF_E_SHUTDOWN;
    return attrs_.CopyTo(a);
  }
  IFACEMETHODIMP GetStreamAttributes(DWORD id, IMFAttributes **a) override {
    std::lock_guard l(mu_);
    if (!a) return E_POINTER;
    if (!queue_) return MF_E_SHUTDOWN;
    if (id != 0) return MF_E_INVALIDSTREAMNUMBER;
    *a = stream_->attributes();
    (*a)->AddRef();
    return S_OK;
  }
  IFACEMETHODIMP SetD3DManager(IUnknown *) override { return S_OK; }

  // IMFGetService
  IFACEMETHODIMP GetService(REFGUID, REFIID, LPVOID *) override { return MF_E_UNSUPPORTED_SERVICE; }

  // IKsControl
  IFACEMETHODIMP KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
  IFACEMETHODIMP KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }
  IFACEMETHODIMP KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG *) override { return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND); }

 private:
  std::mutex mu_;
  ComPtr<IMFMediaEventQueue> queue_;
  ComPtr<IMFAttributes> attrs_;
  ComPtr<IMFPresentationDescriptor> pd_;
  ComPtr<Stream> stream_;
};

// What the Frame Server creates by CLSID: an activator whose ActivateObject makes the source.
#define FORWARD(name, params, args) \
  IFACEMETHODIMP name params override { return attrs_->name args; }

class __declspec(uuid("6B1C2F0E-6A43-4C1B-9E7D-3A5B8C0D2E71")) Activate
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFActivate, IMFAttributes>> {
 public:
  HRESULT RuntimeClassInitialize() { return MFCreateAttributes(&attrs_, 4); }

  IFACEMETHODIMP ActivateObject(REFIID riid, void **ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    std::lock_guard l(mu_);
    if (!source_) {
      ComPtr<Source> s = Make<Source>();
      if (!s) return E_OUTOFMEMORY;
      HRESULT hr = s->init();
      if (FAILED(hr)) return hr;
      source_ = s;
    }
    return source_.CopyTo(riid, ppv);
  }
  IFACEMETHODIMP ShutdownObject() override {
    std::lock_guard l(mu_);
    if (source_) source_->Shutdown();
    source_.Reset();
    return S_OK;
  }
  IFACEMETHODIMP DetachObject() override {
    std::lock_guard l(mu_);
    source_.Reset();
    return S_OK;
  }

  FORWARD(GetItem, (REFGUID k, PROPVARIANT *v), (k, v))
  FORWARD(GetItemType, (REFGUID k, MF_ATTRIBUTE_TYPE *t), (k, t))
  FORWARD(CompareItem, (REFGUID k, REFPROPVARIANT v, BOOL *r), (k, v, r))
  FORWARD(Compare, (IMFAttributes *a, MF_ATTRIBUTES_MATCH_TYPE m, BOOL *r), (a, m, r))
  FORWARD(GetUINT32, (REFGUID k, UINT32 *v), (k, v))
  FORWARD(GetUINT64, (REFGUID k, UINT64 *v), (k, v))
  FORWARD(GetDouble, (REFGUID k, double *v), (k, v))
  FORWARD(GetGUID, (REFGUID k, GUID *v), (k, v))
  FORWARD(GetStringLength, (REFGUID k, UINT32 *n), (k, n))
  FORWARD(GetString, (REFGUID k, LPWSTR v, UINT32 n, UINT32 *len), (k, v, n, len))
  FORWARD(GetAllocatedString, (REFGUID k, LPWSTR *v, UINT32 *n), (k, v, n))
  FORWARD(GetBlobSize, (REFGUID k, UINT32 *n), (k, n))
  FORWARD(GetBlob, (REFGUID k, UINT8 *b, UINT32 n, UINT32 *len), (k, b, n, len))
  FORWARD(GetAllocatedBlob, (REFGUID k, UINT8 **b, UINT32 *n), (k, b, n))
  FORWARD(GetUnknown, (REFGUID k, REFIID riid, LPVOID *p), (k, riid, p))
  FORWARD(SetItem, (REFGUID k, REFPROPVARIANT v), (k, v))
  FORWARD(DeleteItem, (REFGUID k), (k))
  FORWARD(DeleteAllItems, (), ())
  FORWARD(SetUINT32, (REFGUID k, UINT32 v), (k, v))
  FORWARD(SetUINT64, (REFGUID k, UINT64 v), (k, v))
  FORWARD(SetDouble, (REFGUID k, double v), (k, v))
  FORWARD(SetGUID, (REFGUID k, REFGUID v), (k, v))
  FORWARD(SetString, (REFGUID k, LPCWSTR v), (k, v))
  FORWARD(SetBlob, (REFGUID k, const UINT8 *b, UINT32 n), (k, b, n))
  FORWARD(SetUnknown, (REFGUID k, IUnknown *p), (k, p))
  FORWARD(LockStore, (), ())
  FORWARD(UnlockStore, (), ())
  FORWARD(GetCount, (UINT32 * n), (n))
  FORWARD(GetItemByIndex, (UINT32 i, GUID *k, PROPVARIANT *v), (i, k, v))
  FORWARD(CopyAllItems, (IMFAttributes * dst), (dst))

 private:
  std::mutex mu_;
  ComPtr<IMFAttributes> attrs_;
  ComPtr<Source> source_;
};

CoCreatableClass(Activate);

HMODULE g_module = nullptr;

LONG setKey(HKEY root, const std::wstring &path, const wchar_t *name, const std::wstring &value) {
  HKEY k = nullptr;
  LONG r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr);
  if (r != ERROR_SUCCESS) return r;
  r = RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return r;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = inst;
    DisableThreadLibraryCalls(inst);
  }
  return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv) {
  return Module<InProc>::GetModule().GetClassObject(clsid, riid, ppv);
}

STDAPI DllCanUnloadNow() { return Module<InProc>::GetModule().Terminate() ? S_OK : S_FALSE; }

// regsvr32 (the installer, elevated): the class under HKLM so the Frame Server's service finds it.
STDAPI DllRegisterServer() {
  wchar_t path[MAX_PATH] = {};
  if (!GetModuleFileNameW(g_module, path, MAX_PATH)) return HRESULT_FROM_WIN32(GetLastError());
  const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + racg::vcam::kClsidString;
  LONG r = setKey(HKEY_LOCAL_MACHINE, key, nullptr, racg::vcam::kFriendlyName);
  if (r == ERROR_SUCCESS) r = setKey(HKEY_LOCAL_MACHINE, key + L"\\InprocServer32", nullptr, path);
  if (r == ERROR_SUCCESS) r = setKey(HKEY_LOCAL_MACHINE, key + L"\\InprocServer32", L"ThreadingModel", L"Both");
  return HRESULT_FROM_WIN32(r);
}

STDAPI DllUnregisterServer() {
  const std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + racg::vcam::kClsidString;
  const LONG r = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
  return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(r);
}
