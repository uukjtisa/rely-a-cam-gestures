#include "vision/camera.hpp"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <strmif.h>
#include <wrl/client.h>

#include "common/clock.hpp"

#include <QDebug>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <future>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

using Microsoft::WRL::ComPtr;

namespace racg::vision {

namespace {

struct MfInit {
  MfInit() { MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET); }
  ~MfInit() { MFShutdown(); }
};
void ensureMf() { static MfInit init; }

QString fromWide(const wchar_t *w, UINT32 n) { return QString::fromWCharArray(w, int(n)); }

// A COM apartment for the calling thread, for as long as it lives in scope.
struct Com {
  bool ok;
  Com() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
  ~Com() { if (ok) CoUninitialize(); }
};

// DirectShow cameras (OBS Virtual Camera and most other virtual cameras register only there, so
// Media Foundation never lists them) are read through FFmpeg's dshow device. Their id is
// "dshow:<friendly name>".
constexpr const char *kDshow = "dshow:";

void ensureAvDevice() {
  static const bool once = [] {
    avdevice_register_all();
    return true;
  }();
  (void)once;
}

QStringList dshowCameras() {
  ensureAvDevice();
  QStringList out;
  const AVInputFormat *fmt = av_find_input_format("dshow");
  AVDeviceInfoList *list = nullptr;
  if (!fmt || avdevice_list_input_sources(fmt, nullptr, nullptr, &list) < 0 || !list) return out;
  for (int i = 0; i < list->nb_devices; ++i) {
    const AVDeviceInfo *d = list->devices[i];
    bool video = false;
    for (int k = 0; k < d->nb_media_types; ++k) video |= d->media_types[k] == AVMEDIA_TYPE_VIDEO;
    if (video && d->device_description) out << QString::fromUtf8(d->device_description);
  }
  avdevice_free_list_devices(&list);
  return out;
}

}  // namespace

struct Camera::Impl {
  // DirectShow, through FFmpeg.
  AVFormatContext *fmt = nullptr;
  AVCodecContext *dec = nullptr;
  SwsContext *sws = nullptr;
  int stream = -1;
  ~Impl() {
    sws_freeContext(sws);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
  }
  // Media Foundation.
  ComPtr<IMFMediaSource> source;
  ComPtr<IMFSourceReader> reader;
  ComPtr<IAMCameraControl> control;
  UINT32 width = 0, height = 0;
  LONG stride = 0;
};

struct Camera::Session {
  Impl impl;
  std::atomic<bool> stop{false}, running{false};
  std::function<void(const QImage &, qint64)> onFrame;
  std::function<void(const QString &)> onLost;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;   // the capture thread has left (guarded by m)
  void finish() {
    {
      std::lock_guard l(m);
      done = true;
    }
    cv.notify_all();
  }
};

QList<CameraInfo> Camera::list() {
  ensureMf();
  Com com;
  QList<CameraInfo> out;
  ComPtr<IMFAttributes> attr;
  if (FAILED(MFCreateAttributes(&attr, 1))) return out;
  attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
  IMFActivate **devs = nullptr;
  UINT32 n = 0;
  if (FAILED(MFEnumDeviceSources(attr.Get(), &devs, &n))) return out;
  for (UINT32 i = 0; i < n; ++i) {
    wchar_t *name = nullptr, *link = nullptr;
    UINT32 nn = 0, ln = 0;
    if (SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nn)) &&
        SUCCEEDED(devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &ln)))
      out.push_back({fromWide(name, nn), fromWide(link, ln)});
    CoTaskMemFree(name);
    CoTaskMemFree(link);
    devs[i]->Release();
  }
  CoTaskMemFree(devs);
  // Then the DirectShow-only ones (the same camera is never listed twice).
  for (const QString &name : dshowCameras()) {
    bool seen = false;
    for (const auto &c : out) seen |= c.name.compare(name, Qt::CaseInsensitive) == 0;
    if (!seen) out.push_back({name, kDshow + name});
  }
  return out;
}

bool Camera::openDshow(const QString &name) {
  ensureAvDevice();
  auto s = std::make_shared<Session>();
  s->onFrame = onFrame;
  s->onLost = onLost;
  Impl &d = s->impl;
  auto fail = [&](const QString &why) {
    std::lock_guard l(mu_);
    error_ = why;
    return false;
  };
  const AVInputFormat *in = av_find_input_format("dshow");
  d.fmt = avformat_alloc_context();
  // A blocked read gives up as soon as close() asks.
  d.fmt->interrupt_callback = {[](void *ss) { return static_cast<Session *>(ss)->stop.load() ? 1 : 0; }, s.get()};
  AVDictionary *opts = nullptr;
  av_dict_set(&opts, "rtbufsize", "64M", 0);
  const QByteArray url = ("video=" + name).toUtf8();
  if (!in || avformat_open_input(&d.fmt, url.constData(), in, &opts) < 0) {
    av_dict_free(&opts);
    d.fmt = nullptr;
    return fail("The camera couldn't be opened. Another app may be using it.");
  }
  av_dict_free(&opts);
  const AVCodec *codec = nullptr;
  if (avformat_find_stream_info(d.fmt, nullptr) < 0 ||
      (d.stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0 || !codec)
    return fail("The camera opened but its picture couldn't be read.");
  d.dec = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(d.dec, d.fmt->streams[d.stream]->codecpar);
  if (avcodec_open2(d.dec, codec, nullptr) < 0) return fail("The camera's picture format isn't supported.");
  size_ = QSize(d.dec->width, d.dec->height);
  s->running = true;
  session_ = s;
  thread_ = std::thread([s] {
    runDshow(*s);
    s->finish();
  });
  return true;
}

void Camera::runDshow(Session &s) {
  Impl &d = s.impl;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  while (!s.stop) {
    if (av_read_frame(d.fmt, pkt) < 0) {
      s.running = false;
      if (!s.stop && s.onLost) s.onLost("The camera stopped. It may have been closed or taken by another app.");
      break;
    }
    if (pkt->stream_index == d.stream && avcodec_send_packet(d.dec, pkt) >= 0) {
      while (avcodec_receive_frame(d.dec, frame) == 0) {
        d.sws = sws_getCachedContext(d.sws, frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                                     frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!d.sws) continue;
        QImage img(frame->width, frame->height, QImage::Format_RGB32);
        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int lines[4] = {int(img.bytesPerLine()), 0, 0, 0};
        sws_scale(d.sws, frame->data, frame->linesize, 0, frame->height, dst, lines);
        if (!s.stop && s.onFrame) s.onFrame(img, clock::nowMs());
      }
    }
    av_packet_unref(pkt);
  }
  av_frame_free(&frame);
  av_packet_free(&pkt);
  s.running = false;
}

Camera::Camera() = default;
Camera::~Camera() { close(); }

bool Camera::isOpen() const { return session_ && session_->running; }

QString Camera::error() const {
  std::lock_guard l(mu_);
  return error_;
}

bool Camera::open(const QString &id, QSize want) {
  close();
  if (id.startsWith(kDshow)) return openDshow(id.mid(int(strlen(kDshow))));
  ensureMf();
  auto s = std::make_shared<Session>();
  s->onFrame = onFrame;
  s->onLost = onLost;
  auto fail = [&](const QString &why) {
    std::lock_guard l(mu_);
    error_ = why;
    return false;
  };
  // The capture thread owns the COM objects' apartment; set them up there and wait for the answer.
  // Until it answers, open() is waiting, so the thread may still use `this` (fail, size_); after
  // that it touches only the session.
  std::promise<bool> ready;
  auto result = ready.get_future();
  thread_ = std::thread([this, s, id, want, &ready, &fail]() {
    Com com;
    Impl &d = s->impl;
    struct Done {
      Session &s;
      ~Done() { s.finish(); }
    } done{*s};
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 2);
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, reinterpret_cast<LPCWSTR>(id.utf16()));
    if (FAILED(MFCreateDeviceSource(attr.Get(), &d.source))) {
      ready.set_value(fail("The camera couldn't be opened. Another app may be using it."));
      return;
    }
    ComPtr<IMFAttributes> ra;
    MFCreateAttributes(&ra, 1);
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   // YUV / MJPG -> RGB32 for us
    if (FAILED(MFCreateSourceReaderFromMediaSource(d.source.Get(), ra.Get(), &d.reader))) {
      ready.set_value(fail("The camera opened but its picture couldn't be read."));
      return;
    }
    // The native mode closest to the wanted size, at 25 fps or more when there is one.
    ComPtr<IMFMediaType> best;
    double bestCost = 1e18;
    for (DWORD i = 0;; ++i) {
      ComPtr<IMFMediaType> t;
      if (FAILED(d.reader->GetNativeMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &t))) break;
      UINT32 w = 0, h = 0, num = 0, den = 1;
      MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
      MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &num, &den);
      const double fps = den ? double(num) / den : 0;
      const double cost = std::abs(double(w) * h - double(want.width()) * want.height()) / 1e3 +
                          (fps < 24.5 ? 1e9 : 0) + std::abs(double(w) / std::max(h, 1u) - 16.0 / 9) * 100;
      if (cost < bestCost) {
        bestCost = cost;
        best = t;
      }
    }
    if (best) d.reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, best.Get());
    ComPtr<IMFMediaType> out;
    MFCreateMediaType(&out);
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (best) {
      UINT32 w = 0, h = 0;
      MFGetAttributeSize(best.Get(), MF_MT_FRAME_SIZE, &w, &h);
      MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, w, h);
    }
    if (FAILED(d.reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, out.Get()))) {
      ready.set_value(fail("The camera's picture format isn't supported."));
      return;
    }
    ComPtr<IMFMediaType> cur;
    d.reader->GetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &cur);
    MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &d.width, &d.height);
    UINT32 st = 0;
    d.stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) ? LONG(st) : -LONG(d.width * 4);
    d.source.As(&d.control);   // IAMCameraControl, if the driver has it
    size_ = QSize(int(d.width), int(d.height));
    s->running = true;
    ready.set_value(true);   // from here on, nothing of `this`
    run(*s);
  });
  if (!result.get()) {
    thread_.join();
    return false;
  }
  session_ = s;
  return true;
}

void Camera::run(Session &s) {
  Impl &d = s.impl;
  const int w = int(d.width), h = int(d.height);
  while (!s.stop) {
    DWORD stream = 0, flags = 0;
    LONGLONG ts = 0;
    ComPtr<IMFSample> sample;
    const HRESULT hr =
        d.reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &ts, &sample);
    if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
      s.running = false;
      if (!s.stop && s.onLost) s.onLost("The camera stopped. It may have been unplugged or taken by another app.");
      return;
    }
    if (!sample) continue;
    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf))) continue;
    QImage img(w, h, QImage::Format_RGB32);
    BYTE *line0 = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> b2;
    bool locked2d = SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&line0, &pitch));
    BYTE *data = nullptr;
    DWORD len = 0;
    if (!locked2d) {
      if (FAILED(buf->Lock(&data, nullptr, &len))) continue;
      pitch = d.stride;
      line0 = pitch < 0 ? data + size_t(-pitch) * (h - 1) : data;
    }
    for (int y = 0; y < h; ++y) {
      const auto *src = reinterpret_cast<const uint32_t *>(line0 + ptrdiff_t(pitch) * y);
      auto *dst = reinterpret_cast<uint32_t *>(img.scanLine(y));
      for (int x = 0; x < w; ++x) dst[x] = src[x] | 0xFF000000u;   // RGB32's X byte -> opaque
    }
    if (locked2d) b2->Unlock2D();
    else buf->Unlock();
    if (!s.stop && s.onFrame) s.onFrame(img, clock::nowMs());
  }
  s.running = false;
}

void Camera::close() {
  std::shared_ptr<Session> s = std::move(session_);
  if (s) {
    s->stop = true;
    // Shutting the source down usually makes a blocked ReadSample return (MF_E_SHUTDOWN); a dshow
    // read gives up through its interrupt callback.
    if (s->impl.source) s->impl.source->Shutdown();
  }
  if (!thread_.joinable()) return;
  bool finished = true;
  if (s) {
    std::unique_lock l(s->m);
    finished = s->cv.wait_for(l, std::chrono::milliseconds(kCloseWaitMs), [&] { return s->done; });
  }
  if (finished) {
    thread_.join();
  } else {
    // A virtual camera with no picture (Iriun with its phone away hung Quit this way) can keep
    // ReadSample blocked. The thread keeps the session alive and ends with it whenever the driver
    // lets go; nothing it touches belongs to this object.
    qWarning() << "camera: the driver didn't let go of its read; leaving its thread behind";
    thread_.detach();
  }
}

Camera::Range Camera::range(Control c) const {
  Range r;
  if (!session_ || !session_->impl.control) return r;
  long flags = 0;
  const long prop = c == Zoom ? CameraControl_Zoom : c == Pan ? CameraControl_Pan : CameraControl_Tilt;
  r.ok = SUCCEEDED(session_->impl.control->GetRange(prop, &r.min, &r.max, &r.step, &r.def, &flags)) && r.max > r.min;
  return r;
}

bool Camera::set(Control c, long value) {
  if (!session_ || !session_->impl.control) return false;
  const long prop = c == Zoom ? CameraControl_Zoom : c == Pan ? CameraControl_Pan : CameraControl_Tilt;
  return SUCCEEDED(session_->impl.control->Set(prop, value, CameraControl_Flags_Manual));
}

long Camera::get(Control c) const {
  if (!session_ || !session_->impl.control) return 0;
  long v = 0, flags = 0;
  const long prop = c == Zoom ? CameraControl_Zoom : c == Pan ? CameraControl_Pan : CameraControl_Tilt;
  session_->impl.control->Get(prop, &v, &flags);
  return v;
}

}  // namespace racg::vision
