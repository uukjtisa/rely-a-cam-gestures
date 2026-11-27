// OBS's built-in WebSocket (obs-websocket v5, OBS 28+), for the "on its own, OBS open" situation:
// scenes, record, stream and hotkeys. It looks for OBS on localhost every few seconds and
// signs in with the password from Settings (SHA-256 challenge, per the v5 protocol). Qt here has no
// WebSockets module, so the few frames a client needs are written by hand (RFC 6455).
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <functional>
#include <map>

class QTcpSocket;
class QTimer;

namespace racg::obs {

class WebSocket : public QObject {
  Q_OBJECT
 public:
  explicit WebSocket(QObject *parent = nullptr);
  void setTarget(int port, const QString &password);
  void start();
  void stop();

  bool found() const { return identified_; }
  bool needsPassword() const { return authFailed_; }
  bool recording() const { return recording_; }
  bool streaming() const { return streaming_; }
  QStringList scenes() const { return scenes_; }
  QString scene() const { return scene_; }
  QStringList hotkeys() const { return hotkeys_; }

  void setScene(const QString &name);
  void setRecording(bool on);
  void setStreaming(bool on);
  void triggerHotkey(const QString &name);

 Q_SIGNALS:
  void changed();

 private:
  void tryConnect();
  void onConnected();
  void onReadyRead();
  void onMessage(const QByteArray &text);
  void sendJson(int op, const QJsonObject &d);
  void request(const QString &type, const QJsonObject &data = {}, std::function<void(const QJsonObject &)> done = {});
  void sendFrame(const QByteArray &payload, int opcode = 1);
  void refresh();
  void drop();

  QTcpSocket *sock_ = nullptr;
  QTimer *retry_ = nullptr;
  int port_ = 4455;
  QString password_;
  QByteArray buf_, key_;
  bool upgraded_ = false, identified_ = false, authFailed_ = false;
  bool recording_ = false, streaming_ = false;
  QStringList scenes_, hotkeys_;
  QString scene_;
  int nextId_ = 1;
  std::map<QString, std::function<void(const QJsonObject &)>> pending_;
};

}  // namespace racg::obs
