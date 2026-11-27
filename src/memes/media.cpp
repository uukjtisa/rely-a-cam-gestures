#include "memes/media.hpp"

#include <QImageReader>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace racg::memes {

namespace {

QImage fit(QImage img) {
  if (img.isNull()) return img;
  if (std::max(img.width(), img.height()) > kMaxSide)
    img = img.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

class Still : public Media {
 public:
  explicit Still(QImage img) : img_(fit(std::move(img))) {}
  QImage frameAt(double) override { return img_; }
  QSize size() const override { return img_.size(); }

 private:
  QImage img_;
};

// GIFs are small: every frame is read once, with its delay.
class Gif : public Media {
 public:
  bool load(const QString &file) {
    QImageReader r(file);
    r.setAutoTransform(true);
    double t = 0;
    for (int i = 0; i < 600; ++i) {
      QImage f = r.read();
      if (f.isNull()) break;
      const int delay = r.nextImageDelay();
      frames_.push_back(fit(f));
      starts_.push_back(t);
      t += (delay > 10 ? delay : 100) / 1000.0;   // browsers treat 0-10 ms as 100 ms
      if (!r.canRead()) break;
    }
    total_ = t;
    return !frames_.empty();
  }
  QImage frameAt(double t) override {
    if (frames_.size() == 1 || total_ <= 0) return frames_.front();
    t = std::fmod(std::max(0.0, t), total_);
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), t);
    return frames_[size_t(std::max<ptrdiff_t>(0, (it - starts_.begin()) - 1))];
  }
  QSize size() const override { return frames_.front().size(); }

 private:
  std::vector<QImage> frames_;
  std::vector<double> starts_;
  double total_ = 0;
};

// A video, decoded a few frames ahead on its own thread and looped.
class Video : public Media {
 public:
  ~Video() override {
    {
      std::lock_guard l(mu_);
      quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    sws_freeContext(sws_);
    av_frame_free(&frame_);
    av_packet_free(&pkt_);
    avcodec_free_context(&dec_);
    avformat_close_input(&fmt_);
  }

  bool open(const QString &file, QString *error) {
    const QByteArray path = file.toUtf8();
    if (avformat_open_input(&fmt_, path.constData(), nullptr, nullptr) < 0) return fail(error, "can't open the file");
    if (avformat_find_stream_info(fmt_, nullptr) < 0) return fail(error, "can't read the file");
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_ < 0) return fail(error, "no video in it");
    AVStream *st = fmt_->streams[stream_];
    // VP8/VP9 keep their transparency in side data only libvpx reads.
    const AVCodec *codec = nullptr;
    if (st->codecpar->codec_id == AV_CODEC_ID_VP9) codec = avcodec_find_decoder_by_name("libvpx-vp9");
    if (st->codecpar->codec_id == AV_CODEC_ID_VP8) codec = avcodec_find_decoder_by_name("libvpx");
    if (!codec) codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return fail(error, "its video format isn't supported");
    dec_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec_, st->codecpar);
    dec_->thread_count = 2;
    if (avcodec_open2(dec_, codec, nullptr) < 0) return fail(error, "its video can't be decoded");
    tb_ = av_q2d(st->time_base);
    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    QSize s(st->codecpar->width, st->codecpar->height);
    if (s.isEmpty()) return fail(error, "its video has no size");
    if (std::max(s.width(), s.height()) > kMaxSide) s = s.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio);
    size_ = s;
    thread_ = std::thread([this] { run(); });
    return true;
  }

  QImage frameAt(double t) override {
    std::unique_lock l(mu_);
    // Restarted from the top (shown again): drop what was decoded for the old run.
    if (t + 0.05 < lastAsked_) {
      restart_ = true;
      queue_.clear();
      current_ = {};
      cv_.notify_all();
    }
    lastAsked_ = t;
    while (!queue_.empty() && queue_.front().t <= t) {
      current_ = std::move(queue_.front().img);
      queue_.pop_front();
      cv_.notify_all();
    }
    if (current_.isNull() && !queue_.empty()) current_ = queue_.front().img;   // first frame, a little early
    return current_;
  }
  QSize size() const override { return size_; }

 private:
  struct Frame {
    double t;
    QImage img;
  };

  static bool fail(QString *error, const char *why) {
    if (error) *error = why;
    return false;
  }

  QImage convert() {
    sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height, AVPixelFormat(frame_->format), size_.width(),
                                size_.height(), AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) return {};
    QImage img(size_, QImage::Format_ARGB32);
    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int lines[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst, lines);
    // A format without alpha comes out with alpha 0 from some builds: make it opaque.
    const AVPixFmtDescriptor *d = av_pix_fmt_desc_get(AVPixelFormat(frame_->format));
    if (d && !(d->flags & AV_PIX_FMT_FLAG_ALPHA)) img = img.convertToFormat(QImage::Format_RGB32);
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  }

  void run() {
    double loopBase = 0, lastPts = 0, firstPts = -1;
    while (true) {
      {
        std::unique_lock l(mu_);
        cv_.wait(l, [this] { return quit_ || restart_ || queue_.size() < 6; });
        if (quit_) return;
        if (restart_) {
          restart_ = false;
          loopBase = 0;
          firstPts = -1;
          av_seek_frame(fmt_, stream_, 0, AVSEEK_FLAG_BACKWARD);
          avcodec_flush_buffers(dec_);
        }
      }
      const int r = av_read_frame(fmt_, pkt_);
      if (r < 0) {
        // The end: loop from the top, continuing the clock.
        avcodec_send_packet(dec_, nullptr);
        while (avcodec_receive_frame(dec_, frame_) == 0) push(frame_->best_effort_timestamp, loopBase, firstPts, lastPts);
        loopBase += std::max(0.04, lastPts + 0.04);
        av_seek_frame(fmt_, stream_, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec_);
        firstPts = -1;
        if (loopBase > 3600) return;   // a broken file that never moves forward
        continue;
      }
      if (pkt_->stream_index == stream_ && avcodec_send_packet(dec_, pkt_) >= 0)
        while (avcodec_receive_frame(dec_, frame_) == 0) push(frame_->best_effort_timestamp, loopBase, firstPts, lastPts);
      av_packet_unref(pkt_);
    }
  }

  void push(int64_t pts, double loopBase, double &firstPts, double &lastPts) {
    double s = pts == AV_NOPTS_VALUE ? lastPts + 1 / 30.0 : pts * tb_;
    if (firstPts < 0) firstPts = s;
    s -= firstPts;
    lastPts = s;
    QImage img = convert();
    if (img.isNull()) return;
    std::lock_guard l(mu_);
    if (!restart_) queue_.push_back({loopBase + s, std::move(img)});
  }

  AVFormatContext *fmt_ = nullptr;
  AVCodecContext *dec_ = nullptr;
  AVFrame *frame_ = nullptr;
  AVPacket *pkt_ = nullptr;
  SwsContext *sws_ = nullptr;
  int stream_ = -1;
  double tb_ = 0;
  QSize size_;
  std::thread thread_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Frame> queue_;
  QImage current_;
  double lastAsked_ = 0;
  bool quit_ = false, restart_ = false;
};

}  // namespace

std::unique_ptr<Media> Media::open(const QString &file, Kind kind, QString *error) {
  if (kind == Kind::Picture) {
    QImageReader r(file);
    r.setAutoTransform(true);
    QImage img = r.read();
    if (img.isNull()) {
      if (error) *error = r.errorString();
      return nullptr;
    }
    return std::make_unique<Still>(img);
  }
  if (kind == Kind::Gif) {
    auto g = std::make_unique<Gif>();
    if (!g->load(file)) {
      if (error) *error = "the GIF can't be read";
      return nullptr;
    }
    return g;
  }
  auto v = std::make_unique<Video>();
  if (!v->open(file, error)) return nullptr;
  return v;
}

}  // namespace racg::memes
