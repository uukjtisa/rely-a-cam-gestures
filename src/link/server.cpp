#include "link/server.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>

#include <windows.h>

namespace racg::link {

// ---- names ---------------------------------------------------------------------------------------

QString pipeName() {
  wchar_t user[256] = {};
  DWORD n = 256;
  GetUserNameW(user, &n);
  return "Rely-A-Cam-Gestures-link-" + QString::fromWCharArray(user);
}

QString blockName(const char *direction, const QString &device) {
  // FNV-1a over the UTF-8 device id: short, stable, and safe in an object name.
  uint32_t h = 2166136261u;
  for (const char c : device.toUtf8()) {
    h ^= uint8_t(c);
    h *= 16777619u;
  }
  return QString("Local\\RACG.%1.%2").arg(direction).arg(h, 8, 16, QChar('0'));
}

// ---- Block ---------------------------------------------------------------------------------------

Block::~Block() {
  if (view_) UnmapViewOfFile(view_);
  if (handle_) CloseHandle(handle_);
}

bool Block::create(const QString &name, uint32_t slotBytes) {
  const uint64_t size = uint64_t(kHeaderBytes) + 2ull * slotBytes;
  handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(size >> 32), DWORD(size & 0xFFFFFFFF),
                               reinterpret_cast<LPCWSTR>(name.utf16()));
  if (!handle_) return false;
  view_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!view_) return false;
  BlockHeader *h = header();
  memset(h, 0, sizeof(BlockHeader));
  memcpy(h->magic, "RACGLNK1", 8);
  h->slotBytes = slotBytes;
  return true;
}

bool Block::open(const QString &name) {
  handle_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, reinterpret_cast<LPCWSTR>(name.utf16()));
  if (!handle_) return false;
  view_ = MapViewOfFile(handle_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0);
  return view_ && memcmp(header()->magic, "RACGLNK1", 8) == 0;
}

uint8_t *Block::slot(int i) const {
  return static_cast<uint8_t *>(view_) + kHeaderBytes + size_t(i) * header()->slotBytes;
}

// ---- Server --------------------------------------------------------------------------------------

Server::Server(QObject *parent) : QObject(parent) {}
Server::~Server() { drop(); }

bool Server::listen() {
  server_ = new QLocalServer(this);
  server_->setSocketOptions(QLocalServer::UserAccessOption);
  QLocalServer::removeServer(pipeName());
  if (!server_->listen(pipeName())) {
    qWarning() << "link: can't listen:" << server_->errorString();
    return false;
  }
  connect(server_, &QLocalServer::newConnection, this, &Server::onConnection);
  return true;
}

void Server::onConnection() {
  while (QLocalSocket *s = server_->nextPendingConnection()) {
    if (sock_) {
      // One plugin at a time: a second OBS gets a polite no.
      s->write(QByteArray(1, char(Msg::Bye)) + QByteArray("\0\0\0\x13", 4) + R"({"reason":"in use"})");
      s->flush();
      s->disconnectFromServer();
      s->deleteLater();
      continue;
    }
    sock_ = s;
    hello_ = false;
    buf_.clear();
    connect(sock_, &QLocalSocket::readyRead, this, &Server::onReadyRead);
    connect(sock_, &QLocalSocket::disconnected, this, &Server::drop);
  }
}

void Server::drop() {
  if (!sock_) return;
  const bool was = hello_;
  sock_->disconnect(this);
  sock_->deleteLater();
  sock_ = nullptr;
  hello_ = false;
  in_.clear();
  out_.clear();
  outSeq_.clear();
  if (was) Q_EMIT detached();
}

void Server::onReadyRead() {
  buf_ += sock_->readAll();
  while (buf_.size() >= 5) {
    const uint32_t len = (uint32_t(uint8_t(buf_[1])) << 24) | (uint32_t(uint8_t(buf_[2])) << 16) |
                         (uint32_t(uint8_t(buf_[3])) << 8) | uint32_t(uint8_t(buf_[4]));
    if (len > kMaxMessage) {
      qWarning() << "link: oversized message, dropping the plugin";
      drop();
      return;
    }
    if (uint32_t(buf_.size()) < 5 + len) return;
    const Msg type = Msg(uint8_t(buf_[0]));
    const QByteArray payload = buf_.mid(5, int(len));
    buf_.remove(0, int(5 + len));
    handle(type, payload);
    if (!sock_) return;
  }
}

void Server::handle(Msg type, const QByteArray &payload) {
  if (type == Msg::Upload) {
    if (payload.size() < 4) return;
    const uint32_t n = (uint32_t(uint8_t(payload[0])) << 24) | (uint32_t(uint8_t(payload[1])) << 16) |
                       (uint32_t(uint8_t(payload[2])) << 8) | uint32_t(uint8_t(payload[3]));
    if (4 + qsizetype(n) > payload.size()) return;
    Q_EMIT upload(QString::fromUtf8(payload.mid(4, int(n))), payload.mid(4 + int(n)));
    return;
  }
  const QJsonObject j = QJsonDocument::fromJson(payload).object();
  if (type == Msg::Hello) {
    if (j["v"].toInt() != kVersion) {
      qWarning() << "link: plugin speaks version" << j["v"].toInt() << ", this app" << kVersion;
      writeRaw(Msg::Bye, QJsonDocument(QJsonObject{{"reason", "version"}, {"v", kVersion}}).toJson(QJsonDocument::Compact));
      sock_->flush();
      drop();
      return;
    }
    pluginVersion_ = j["version"].toString();
    hello_ = true;
    send(Msg::Hello, {{"v", kVersion}, {"app", "rely-a-cam-gestures"}, {"version", QCoreApplication::applicationVersion()}});
    Q_EMIT attached();
    return;
  }
  if (!hello_) return;
  if (type == Msg::Frame) {
    const QString device = j["device"].toString();
    auto &b = in_[device];
    if (!b) {
      b = std::make_unique<Block>();
      if (!b->open(blockName("in", device))) {
        in_.erase(device);
        return;
      }
    }
    const int slot = j["slot"].toInt() & 1;
    const uint64_t seq = uint64_t(j["seq"].toDouble());
    const int w = j["w"].toInt(), h = j["h"].toInt(), stride = j["stride"].toInt();
    if (w <= 0 || h <= 0 || stride < w * 4 || uint64_t(stride) * h > b->slotBytes()) return;
    QImage img(w, h, QImage::Format_RGB32);
    const uint8_t *src = b->slot(slot);
    for (int y = 0; y < h; ++y) memcpy(img.scanLine(y), src + size_t(stride) * y, size_t(w) * 4);
    if (b->header()->seq[slot] != seq) return;   // overwritten while copying: the next one is coming
    Q_EMIT frame(device, img, QSize(j["outW"].toInt(w), j["outH"].toInt(h)));
    return;
  }
  if (type == Msg::Bye) {
    drop();
    return;
  }
  Q_EMIT message(type, j);
}

void Server::writeRaw(Msg type, const QByteArray &payload) {
  if (!sock_) return;
  QByteArray head(5, 0);
  head[0] = char(type);
  const uint32_t n = uint32_t(payload.size());
  head[1] = char(n >> 24);
  head[2] = char(n >> 16);
  head[3] = char(n >> 8);
  head[4] = char(n);
  sock_->write(head + payload);
}

void Server::send(Msg type, const QJsonObject &json) {
  if (!connected() && type != Msg::Hello) return;
  writeRaw(type, QJsonDocument(json).toJson(QJsonDocument::Compact));
}

void Server::sendLayer(const QString &device, const QImage &layer, bool empty, const QRectF &crop) {
  if (!connected()) return;
  QJsonObject j{{"device", device}, {"empty", empty},
                {"crop", QJsonArray{crop.x(), crop.y(), crop.width(), crop.height()}}};
  if (!empty && !layer.isNull()) {
    auto &b = out_[device];
    if (!b) {
      b = std::make_unique<Block>();
      if (!b->create(blockName("out", device), kOutSlotBytes)) {
        out_.erase(device);
        return;
      }
    }
    const uint64_t seq = ++outSeq_[device];
    const int slot = int(seq & 1);
    const int w = layer.width(), h = layer.height(), stride = w * 4;
    if (uint64_t(stride) * h > b->slotBytes()) return;
    uint8_t *dst = b->slot(slot);
    for (int y = 0; y < h; ++y) memcpy(dst + size_t(stride) * y, layer.constScanLine(y), size_t(stride));
    BlockHeader *hd = b->header();
    hd->w[slot] = uint32_t(w);
    hd->h[slot] = uint32_t(h);
    hd->stride[slot] = uint32_t(stride);
    hd->seq[slot] = seq;
    j["slot"] = slot;
    j["seq"] = double(seq);
    j["w"] = w;
    j["h"] = h;
    j["stride"] = stride;
  }
  send(Msg::Layer, j);
}

void Server::sendAudio(const float *lr, int frames) {
  QByteArray p(4 + frames * 8, 0);
  p[0] = char(uint32_t(frames) >> 24);
  p[1] = char(uint32_t(frames) >> 16);
  p[2] = char(uint32_t(frames) >> 8);
  p[3] = char(uint32_t(frames));
  memcpy(p.data() + 4, lr, size_t(frames) * 8);
  QMetaObject::invokeMethod(this, [this, p] {
    if (connected()) writeRaw(Msg::Audio, p);
  }, Qt::QueuedConnection);
}

void Server::bye(const QString &reason) {
  if (!sock_) return;
  writeRaw(Msg::Bye, QJsonDocument(QJsonObject{{"reason", reason}}).toJson(QJsonDocument::Compact));
  sock_->flush();
  sock_->waitForBytesWritten(500);
}

}  // namespace racg::link
