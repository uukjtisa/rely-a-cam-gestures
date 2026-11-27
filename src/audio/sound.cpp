#include "audio/sound.hpp"

#include <QDebug>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <deque>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace racg::audio {

using Microsoft::WRL::ComPtr;

namespace {

const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr int kMaxSeconds = 60;

struct Com {
  bool ok;
  Com() : ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {}
  ~Com() {
    if (ok) CoUninitialize();
  }
};

ComPtr<IMMDeviceEnumerator> enumerator() {
  ComPtr<IMMDeviceEnumerator> e;
  CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e));
  return e;
}

std::wstring lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
  return s;
}

// (id, name) of every active endpoint of one direction.
std::vector<std::pair<std::wstring, std::wstring>> endpoints(EDataFlow flow) {
  std::vector<std::pair<std::wstring, std::wstring>> out;
  auto e = enumerator();
  ComPtr<IMMDeviceCollection> c;
  if (!e || FAILED(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &c))) return out;
  UINT n = 0;
  c->GetCount(&n);
  for (UINT i = 0; i < n; i++) {
    ComPtr<IMMDevice> d;
    ComPtr<IPropertyStore> ps;
    if (FAILED(c->Item(i, &d)) || FAILED(d->OpenPropertyStore(STGM_READ, &ps))) continue;
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring name;
    if (SUCCEEDED(ps->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) name = v.pwszVal;
    PropVariantClear(&v);
    LPWSTR id = nullptr;
    if (SUCCEEDED(d->GetId(&id)) && id) {
      out.emplace_back(id, name);
      CoTaskMemFree(id);
    }
  }
  return out;
}

std::wstring findEndpoint(EDataFlow flow, const wchar_t *part) {
  for (const auto &[id, name] : endpoints(flow))
    if (lower(name).find(part) != std::wstring::npos) return id;
  return {};
}

WAVEFORMATEXTENSIBLE floatStereo() {
  WAVEFORMATEXTENSIBLE f = {};
  f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  f.Format.nChannels = 2;
  f.Format.nSamplesPerSec = kRate;
  f.Format.wBitsPerSample = 32;
  f.Format.nBlockAlign = 8;
  f.Format.nAvgBytesPerSec = kRate * 8;
  f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  f.Samples.wValidBitsPerSample = 32;
  f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  f.SubFormat = kFloat;
  return f;
}

// One WASAPI stream in shared mode, 48 kHz float stereo (Windows converts to the device's format).
struct Stream {
  ComPtr<IAudioClient> client;
  ComPtr<IAudioRenderClient> render;
  ComPtr<IAudioCaptureClient> capture;
  UINT32 bufferFrames = 0;
  bool started = false;

  bool open(EDataFlow flow, const std::wstring &id) {
    auto e = enumerator();
    ComPtr<IMMDevice> dev;
    if (!e) return false;
    HRESULT hr = id.empty() ? e->GetDefaultAudioEndpoint(flow, flow == eRender ? eConsole : eCommunications, &dev)
                            : e->GetDevice(id.c_str(), &dev);
    if (FAILED(hr) || FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                           reinterpret_cast<void **>(client.GetAddressOf()))))
      return false;
    WAVEFORMATEXTENSIBLE f = floatStereo();
    const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &f.Format, nullptr)) ||
        FAILED(client->GetBufferSize(&bufferFrames)))
      return false;
    hr = flow == eRender ? client->GetService(IID_PPV_ARGS(&render)) : client->GetService(IID_PPV_ARGS(&capture));
    if (FAILED(hr)) return false;
    started = SUCCEEDED(client->Start());
    return started;
  }
  void close() {
    if (client && started) client->Stop();
    render.Reset();
    capture.Reset();
    client.Reset();
    started = false;
  }
  // Frames the render side can take now; -1 when the device went away.
  int room() {
    UINT32 pad = 0;
    if (FAILED(client->GetCurrentPadding(&pad))) return -1;
    return int(bufferFrames - pad);
  }
  bool write(const float *lr, int frames) {
    BYTE *buf = nullptr;
    if (FAILED(render->GetBuffer(UINT32(frames), &buf))) return false;
    memcpy(buf, lr, size_t(frames) * 8);
    return SUCCEEDED(render->ReleaseBuffer(UINT32(frames), 0));
  }
  // Appends whatever the mic has to `fifo`; false when the device went away.
  bool read(std::deque<float> &fifo) {
    UINT32 n = 0;
    while (true) {
      if (FAILED(capture->GetNextPacketSize(&n))) return false;
      if (n == 0) return true;
      BYTE *data = nullptr;
      DWORD flags = 0;
      if (FAILED(capture->GetBuffer(&data, &n, &flags, nullptr, nullptr))) return false;
      const float *f = reinterpret_cast<const float *>(data);
      for (UINT32 i = 0; i < n * 2; ++i) fifo.push_back((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.f : f[i]);
      capture->ReleaseBuffer(n);
    }
  }
};

}  // namespace

// ---- decoding --------------------------------------------------------------------------------

std::shared_ptr<const Clip> load(const QString &file) {
  AVFormatContext *fmt = nullptr;
  const QByteArray path = file.toUtf8();
  if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) < 0) return nullptr;
  std::shared_ptr<Clip> clip;
  AVCodecContext *dec = nullptr;
  SwrContext *swr = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVPacket *pkt = av_packet_alloc();
  int s = -1;
  const AVCodec *codec = nullptr;
  if (avformat_find_stream_info(fmt, nullptr) >= 0 && (s = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0)) >= 0) {
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, fmt->streams[s]->codecpar);
    if (avcodec_open2(dec, codec, nullptr) >= 0) {
      AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
      if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &dec->ch_layout, dec->sample_fmt, dec->sample_rate, 0,
                              nullptr) >= 0 &&
          swr_init(swr) >= 0) {
        clip = std::make_shared<Clip>();
        auto drain = [&] {
          while (avcodec_receive_frame(dec, frame) == 0) {
            const int room = swr_get_out_samples(swr, frame->nb_samples);
            const size_t at = clip->lr.size();
            clip->lr.resize(at + size_t(room) * 2);
            uint8_t *out = reinterpret_cast<uint8_t *>(clip->lr.data() + at);
            const int got = swr_convert(swr, &out, room, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
            clip->lr.resize(at + size_t(std::max(0, got)) * 2);
          }
        };
        while (av_read_frame(fmt, pkt) >= 0 && clip->frames() < kRate * kMaxSeconds) {
          if (pkt->stream_index == s && avcodec_send_packet(dec, pkt) >= 0) drain();
          av_packet_unref(pkt);
        }
        avcodec_send_packet(dec, nullptr);
        drain();
      }
    }
  }
  swr_free(&swr);
  av_packet_free(&pkt);
  av_frame_free(&frame);
  avcodec_free_context(&dec);
  avformat_close_input(&fmt);
  if (clip && clip->frames() == 0) clip.reset();
  return clip;
}

bool hasAudio(const QString &file) {
  AVFormatContext *fmt = nullptr;
  const QByteArray path = file.toUtf8();
  if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) < 0) return false;
  const bool yes = avformat_find_stream_info(fmt, nullptr) >= 0 && av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
  avformat_close_input(&fmt);
  return yes;
}

bool cableInstalled() {
  Com com;
  return !findEndpoint(eRender, L"cable input").empty();
}

QStringList microphones() {
  Com com;
  QStringList out;
  for (const auto &[id, name] : endpoints(eCapture)) {
    const QString n = QString::fromStdWString(name);
    if (n.contains("CABLE Output", Qt::CaseInsensitive)) continue;   // our own output: it would echo
    out << QString::fromStdWString(id) + "|" + n;
  }
  return out;
}

// ---- the mixer ---------------------------------------------------------------------------------

Mixer::Mixer() { thread_ = std::thread([this] { run(); }); }

Mixer::~Mixer() {
  quit_ = true;
  if (thread_.joinable()) thread_.join();
}

void Mixer::setRoute(Route r, const QString &micId) {
  std::lock_guard l(mu_);
  if (r == route_ && micId == micId_) return;
  route_ = r;
  micId_ = micId;
  reopen_ = true;
}

void Mixer::play(const QString &key, std::shared_ptr<const Clip> clip, float volume, bool loop) {
  if (!clip) return;
  std::lock_guard l(mu_);
  voices_[key] = {std::move(clip), volume, loop, 0};
}

void Mixer::stop(const QString &key) {
  std::lock_guard l(mu_);
  voices_.erase(key);
}

void Mixer::stopAll() {
  std::lock_guard l(mu_);
  voices_.clear();
}

QString Mixer::error() const {
  std::lock_guard l(mu_);
  return error_;
}

void Mixer::mix(float *out, int frames) {
  std::fill(out, out + frames * 2, 0.f);
  std::lock_guard l(mu_);
  for (auto it = voices_.begin(); it != voices_.end();) {
    Voice &v = it->second;
    const int n = v.clip->frames();
    for (int i = 0; i < frames; ++i) {
      if (v.pos >= n) {
        if (!v.loop) break;
        v.pos = 0;
      }
      out[2 * i] += v.clip->lr[size_t(v.pos) * 2] * v.volume;
      out[2 * i + 1] += v.clip->lr[size_t(v.pos) * 2 + 1] * v.volume;
      ++v.pos;
    }
    if (!v.loop && v.pos >= n) it = voices_.erase(it);
    else ++it;
  }
}

void Mixer::run() {
  Com com;
  Stream out, mic;
  std::deque<float> micFifo;
  std::vector<float> block(kRate / 100 * 2 * 8);
  auto next = std::chrono::steady_clock::now();
  bool openFailed = false;
  while (!quit_) {
    if (reopen_.exchange(false)) {
      out.close();
      mic.close();
      micFifo.clear();
      openFailed = false;
      Route r;
      QString micId;
      {
        std::lock_guard l(mu_);
        r = route_;
        micId = micId_;
      }
      QString err;
      if (r == Route::Speakers) {
        if (!out.open(eRender, {})) err = "No speakers to play meme sound on";
      } else if (r == Route::Calls) {
        const std::wstring cable = findEndpoint(eRender, L"cable input");
        if (cable.empty()) err = "VB-Cable isn't installed, so meme sound can't go into calls";
        else if (!out.open(eRender, cable)) err = "CABLE Input refused the meme sound";
        else if (!mic.open(eCapture, micId.toStdWString())) err = "Your mic couldn't be opened to mix into calls";
      }
      openFailed = !err.isEmpty();
      std::lock_guard l(mu_);
      if (err != error_ && !err.isEmpty()) qWarning() << "sound:" << err;
      error_ = err;
    }
    const Route r = route_;
    if (r == Route::Link) {
      // Real time, 10 ms at a time.
      next += std::chrono::milliseconds(10);
      std::this_thread::sleep_until(next);
      const int n = kRate / 100;
      mix(block.data(), n);
      bool any = false;
      for (int i = 0; i < n * 2 && !any; ++i) any = block[size_t(i)] != 0.f;
      if (any && onLink) onLink(block.data(), n);
      continue;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    next = std::chrono::steady_clock::now();
    if (openFailed || !out.started) {
      // Retry now and then (the cable got installed, the headphones came back).
      static int wait = 0;
      if (++wait > 300) {
        wait = 0;
        reopen_ = true;
      }
      std::lock_guard l(mu_);
      voices_.clear();
      continue;
    }
    // Keep about 40 ms queued in the device.
    int room = out.room();
    if (room < 0) {
      reopen_ = true;
      continue;
    }
    const int queued = int(out.bufferFrames) - room;
    int want = std::min(room, std::max(0, kRate * 40 / 1000 - queued));
    if (r == Route::Calls) {
      if (!mic.read(micFifo)) {
        reopen_ = true;
        continue;
      }
      while (micFifo.size() > size_t(kRate / 10) * 2) micFifo.pop_front();   // never more than 100 ms behind
    }
    while (want > 0) {
      const int n = std::min(want, int(block.size() / 2));
      mix(block.data(), n);
      if (r == Route::Calls)
        for (int i = 0; i < n * 2 && !micFifo.empty(); ++i) {
          block[size_t(i)] += micFifo.front();
          micFifo.pop_front();
        }
      for (int i = 0; i < n * 2; ++i) block[size_t(i)] = std::clamp(block[size_t(i)], -1.f, 1.f);
      if (!out.write(block.data(), n)) {
        reopen_ = true;
        break;
      }
      want -= n;
    }
  }
  out.close();
  mic.close();
}

}  // namespace racg::audio
