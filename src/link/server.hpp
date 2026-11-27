// This app's end of the link: it always listens, and Rely-A-Cam's plugin connects when a phone
// has gestures on (starting the app with --attach if it isn't running). One plugin at a time.
#pragma once

#include "link/wire.hpp"

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QRectF>

#include <map>
#include <memory>

class QLocalServer;
class QLocalSocket;

namespace racg::link {

// A shared memory block (Win32 file mapping), made by the side that writes it.
class Block {
 public:
  ~Block();
  bool create(const QString &name, uint32_t slotBytes);
  bool open(const QString &name);
  BlockHeader *header() const { return static_cast<BlockHeader *>(view_); }
  uint8_t *slot(int i) const;
  uint32_t slotBytes() const { return header() ? header()->slotBytes : 0; }

 private:
  void *handle_ = nullptr, *view_ = nullptr;
};

class Server : public QObject {
  Q_OBJECT
 public:
  explicit Server(QObject *parent = nullptr);
  ~Server() override;
  bool listen();
  bool connected() const { return sock_ != nullptr && hello_; }
  QString pluginVersion() const { return pluginVersion_; }

  void send(Msg type, const QJsonObject &json);
  void sendLayer(const QString &device, const QImage &layer, bool empty, const QRectF &crop);
  void sendAudio(const float *lr, int frames);   // any thread
  void bye(const QString &reason);

 Q_SIGNALS:
  void attached();
  void detached();
  void message(link::Msg type, const QJsonObject &json);
  void frame(const QString &device, const QImage &picture, QSize out);
  void upload(const QString &name, const QByteArray &bytes);

 private:
  void onConnection();
  void onReadyRead();
  void handle(Msg type, const QByteArray &payload);
  void drop();
  void writeRaw(Msg type, const QByteArray &payload);

  QLocalServer *server_ = nullptr;
  QLocalSocket *sock_ = nullptr;
  QByteArray buf_;
  bool hello_ = false;
  QString pluginVersion_;
  std::map<QString, std::unique_ptr<Block>> in_, out_;
  std::map<QString, uint64_t> outSeq_;
};

}  // namespace racg::link
