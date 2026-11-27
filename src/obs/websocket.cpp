#include "obs/websocket.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

namespace racg::obs {

namespace {
// obs-websocket v5 opcodes and event subscriptions.
enum Op { Hello = 0, Identify = 1, Identified = 2, Event = 5, Request = 6, RequestResponse = 7 };
constexpr int kSubGeneral = 1, kSubScenes = 4, kSubOutputs = 64;
}  // namespace

WebSocket::WebSocket(QObject *parent) : QObject(parent) {
  retry_ = new QTimer(this);
  retry_->setInterval(4000);
  connect(retry_, &QTimer::timeout, this, &WebSocket::tryConnect);
}

void WebSocket::setTarget(int port, const QString &password) {
  if (port == port_ && password == password_) return;
  port_ = port;
  password_ = password;
  authFailed_ = false;
  drop();
  tryConnect();
}

void WebSocket::start() {
  retry_->start();
  tryConnect();
}

void WebSocket::stop() {
  retry_->stop();
  drop();
}

void WebSocket::drop() {
  const bool was = identified_;
  if (sock_) {
    sock_->disconnect(this);
    sock_->abort();
    sock_->deleteLater();
    sock_ = nullptr;
  }
  upgraded_ = identified_ = false;
  buf_.clear();
  pending_.clear();
  recording_ = streaming_ = false;
  scenes_.clear();
  hotkeys_.clear();
  if (was) Q_EMIT changed();
}

void WebSocket::tryConnect() {
  if (sock_ || authFailed_) return;
  sock_ = new QTcpSocket(this);
  connect(sock_, &QTcpSocket::connected, this, &WebSocket::onConnected);
  connect(sock_, &QTcpSocket::readyRead, this, &WebSocket::onReadyRead);
  connect(sock_, &QTcpSocket::disconnected, this, [this] { drop(); });
  connect(sock_, &QTcpSocket::errorOccurred, this, [this] { drop(); });
  sock_->connectToHost("127.0.0.1", quint16(port_));
}

void WebSocket::onConnected() {
  QByteArray k(16, 0);
  for (auto &c : k) c = char(QRandomGenerator::global()->bounded(256));
  key_ = k.toBase64();
  const QByteArray req = "GET / HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port_) +
                         "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key_ +
                         "\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: obswebsocket.json\r\n\r\n";
  sock_->write(req);
}

void WebSocket::onReadyRead() {
  buf_ += sock_->readAll();
  if (!upgraded_) {
    const int end = buf_.indexOf("\r\n\r\n");
    if (end < 0) return;
    const QByteArray head = buf_.left(end);
    buf_.remove(0, end + 4);
    if (!head.startsWith("HTTP/1.1 101")) {
      drop();
      return;
    }
    upgraded_ = true;
  }
  // Server frames are never masked.
  while (sock_ && buf_.size() >= 2) {
    const quint8 b0 = quint8(buf_[0]), b1 = quint8(buf_[1]);
    const int op = b0 & 0x0F;
    qint64 len = b1 & 0x7F;
    int at = 2;
    if (len == 126) {
      if (buf_.size() < 4) return;
      len = (quint8(buf_[2]) << 8) | quint8(buf_[3]);
      at = 4;
    } else if (len == 127) {
      if (buf_.size() < 10) return;
      len = 0;
      for (int i = 0; i < 8; ++i) len = (len << 8) | quint8(buf_[2 + i]);
      at = 10;
    }
    if (buf_.size() < at + len) return;
    const QByteArray payload = buf_.mid(at, len);
    buf_.remove(0, int(at + len));
    if (op == 1) onMessage(payload);
    else if (op == 9) sendFrame(payload, 10);   // ping -> pong
    else if (op == 8) {
      // Close: 4009 means the password was wrong.
      if (payload.size() >= 2 && ((quint8(payload[0]) << 8) | quint8(payload[1])) == 4009) authFailed_ = true;
      drop();
      Q_EMIT changed();
      return;
    }
  }
}

void WebSocket::sendFrame(const QByteArray &payload, int opcode) {
  if (!sock_) return;
  QByteArray f;
  f.append(char(0x80 | opcode));
  const qint64 n = payload.size();
  if (n < 126) {
    f.append(char(0x80 | n));
  } else if (n < 65536) {
    f.append(static_cast<char>(uint8_t(0xFE)));   // masked, 16-bit length
    f.append(char(n >> 8));
    f.append(char(n & 0xFF));
  } else {
    f.append(static_cast<char>(uint8_t(0xFF)));   // masked, 64-bit length
    for (int i = 7; i >= 0; --i) f.append(char((n >> (8 * i)) & 0xFF));
  }
  char mask[4];
  for (auto &m : mask) m = char(QRandomGenerator::global()->bounded(256));
  f.append(mask, 4);
  QByteArray body = payload;
  for (qint64 i = 0; i < n; ++i) body[i] = char(body[i] ^ mask[i % 4]);
  f.append(body);
  sock_->write(f);
}

void WebSocket::sendJson(int op, const QJsonObject &d) {
  sendFrame(QJsonDocument(QJsonObject{{"op", op}, {"d", d}}).toJson(QJsonDocument::Compact));
}

void WebSocket::request(const QString &type, const QJsonObject &data, std::function<void(const QJsonObject &)> done) {
  if (!identified_) return;
  const QString id = QString::number(nextId_++);
  if (done) pending_[id] = std::move(done);
  QJsonObject d{{"requestType", type}, {"requestId", id}};
  if (!data.isEmpty()) d["requestData"] = data;
  sendJson(Request, d);
}

void WebSocket::onMessage(const QByteArray &text) {
  const QJsonObject msg = QJsonDocument::fromJson(text).object();
  const int op = msg["op"].toInt(-1);
  const QJsonObject d = msg["d"].toObject();
  switch (op) {
    case Hello: {
      QJsonObject id{{"rpcVersion", 1}, {"eventSubscriptions", kSubGeneral | kSubScenes | kSubOutputs}};
      const QJsonObject auth = d["authentication"].toObject();
      if (!auth.isEmpty()) {
        // base64(sha256(base64(sha256(password + salt)) + challenge))
        const QByteArray secret =
            QCryptographicHash::hash(password_.toUtf8() + auth["salt"].toString().toUtf8(), QCryptographicHash::Sha256).toBase64();
        id["authentication"] = QString::fromLatin1(
            QCryptographicHash::hash(secret + auth["challenge"].toString().toUtf8(), QCryptographicHash::Sha256).toBase64());
      }
      sendJson(Identify, id);
      break;
    }
    case Identified:
      identified_ = true;
      authFailed_ = false;
      refresh();
      Q_EMIT changed();
      break;
    case RequestResponse: {
      auto it = pending_.find(d["requestId"].toString());
      if (it == pending_.end()) break;
      auto fn = std::move(it->second);
      pending_.erase(it);
      if (d["requestStatus"].toObject()["result"].toBool()) fn(d["responseData"].toObject());
      break;
    }
    case Event: {
      const QString type = d["eventType"].toString();
      const QJsonObject data = d["eventData"].toObject();
      if (type == "RecordStateChanged") {
        recording_ = data["outputActive"].toBool();
        Q_EMIT changed();
      } else if (type == "StreamStateChanged") {
        streaming_ = data["outputActive"].toBool();
        Q_EMIT changed();
      } else if (type == "CurrentProgramSceneChanged") {
        scene_ = data["sceneName"].toString();
        Q_EMIT changed();
      } else if (type.startsWith("Scene") || type == "SceneListChanged") {
        refresh();
      }
      break;
    }
    default: break;
  }
}

void WebSocket::refresh() {
  request("GetSceneList", {}, [this](const QJsonObject &r) {
    scenes_.clear();
    const QJsonArray a = r["scenes"].toArray();
    // OBS lists them bottom-up; show them top-down like its Scenes dock.
    for (auto it = a.end(); it != a.begin();) scenes_ << (--it)->toObject()["sceneName"].toString();
    scene_ = r["currentProgramSceneName"].toString();
    Q_EMIT changed();
  });
  request("GetRecordStatus", {}, [this](const QJsonObject &r) {
    recording_ = r["outputActive"].toBool();
    Q_EMIT changed();
  });
  request("GetStreamStatus", {}, [this](const QJsonObject &r) {
    streaming_ = r["outputActive"].toBool();
    Q_EMIT changed();
  });
  request("GetHotkeyList", {}, [this](const QJsonObject &r) {
    hotkeys_.clear();
    for (const auto &h : r["hotkeys"].toArray()) hotkeys_ << h.toString();
    hotkeys_.sort(Qt::CaseInsensitive);
    Q_EMIT changed();
  });
}

void WebSocket::setScene(const QString &name) { request("SetCurrentProgramScene", {{"sceneName", name}}); }
void WebSocket::setRecording(bool on) { request(on ? "StartRecord" : "StopRecord"); }
void WebSocket::setStreaming(bool on) { request(on ? "StartStream" : "StopStream"); }
void WebSocket::triggerHotkey(const QString &name) { request("TriggerHotkeyByName", {{"hotkeyName", name}}); }

}  // namespace racg::obs
