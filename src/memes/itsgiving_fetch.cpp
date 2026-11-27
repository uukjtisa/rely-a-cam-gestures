#include "memes/itsgiving_fetch.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace racg::memes {

namespace {

constexpr qint64 kMaxBytes = 15 * 1024 * 1024;   // a picture or GIF; itsgiving's are under 2 MB
constexpr int kMaxFiles = 64;
constexpr int kListTimeoutMs = 15000, kFileTimeoutMs = 60000;

bool isPicture(const QString &name) {
  const QString e = QFileInfo(name).suffix().toLower();
  return e == "gif" || e == "png" || e == "jpg" || e == "jpeg";
}

QNetworkRequest request(const QUrl &url, int timeoutMs) {
  QNetworkRequest r(url);
  r.setHeader(QNetworkRequest::UserAgentHeader, "Rely-A-Cam-Gestures");
  r.setTransferTimeout(timeoutMs);
  return r;
}

}  // namespace

void fetchItsgiving(QObject *context, FetchDone done) {
  auto *net = new QNetworkAccessManager(context);
  auto folder = std::make_shared<QTemporaryDir>();
  if (!folder->isValid()) {
    net->deleteLater();
    done(nullptr, "no temporary folder");
    return;
  }
  QNetworkRequest listReq = request(
      QUrl(QString("https://api.github.com/repos/%1/contents/assets?ref=%2").arg(kItsgivingRepo, kItsgivingCommit)),
      kListTimeoutMs);
  listReq.setRawHeader("Accept", "application/vnd.github+json");
  QNetworkReply *list = net->get(listReq);
  QObject::connect(list, &QNetworkReply::finished, context, [=] {
    list->deleteLater();
    auto fail = [=](const QString &why) {
      net->deleteLater();
      done(nullptr, why);
    };
    if (list->error() != QNetworkReply::NoError) return fail(list->errorString());

    struct Want {
      QString name;
      QUrl url;
    };
    QList<Want> wants;
    for (const QJsonValue &v : QJsonDocument::fromJson(list->readAll()).array()) {
      const QJsonObject o = v.toObject();
      // Only a bare file name is ever used locally, never a path from the web.
      const QString name = QFileInfo(o["name"].toString()).fileName();
      const QUrl url(o["download_url"].toString());
      if (o["type"].toString() != "file" || !isPicture(name) || o["size"].toInteger() > kMaxBytes) continue;
      if (url.scheme() != "https" || url.host() != "raw.githubusercontent.com") continue;
      wants.append({name, url});
      if (wants.size() == kMaxFiles) break;
    }
    if (wants.isEmpty()) return fail("itsgiving's assets folder has no pictures at that commit");

    auto left = std::make_shared<int>(int(wants.size()));
    auto saved = std::make_shared<int>(0);
    for (const Want &w : wants) {
      QNetworkReply *rep = net->get(request(w.url, kFileTimeoutMs));
      QObject::connect(rep, &QNetworkReply::finished, context, [=] {
        rep->deleteLater();
        if (rep->error() == QNetworkReply::NoError) {
          const QByteArray bytes = rep->read(kMaxBytes + 1);
          QFile f(folder->filePath(w.name));
          if (bytes.size() > 0 && bytes.size() <= kMaxBytes && f.open(QIODevice::WriteOnly) &&
              f.write(bytes) == bytes.size())
            ++*saved;
        }
        if (--*left > 0) return;
        net->deleteLater();
        if (*saved) done(folder, {});
        else done(nullptr, "every download failed");
      });
    }
  });
}

}  // namespace racg::memes
