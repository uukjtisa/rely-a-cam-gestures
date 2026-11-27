#include "common/settings.hpp"

#include <Windows.h>
#include <dpapi.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace racg {

namespace {

constexpr int kVersion = 1;

QString protect(const QString &plain) {
  if (plain.isEmpty()) return {};
  const QByteArray in = plain.toUtf8();
  DATA_BLOB a{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))}, out{};
  if (!CryptProtectData(&a, L"Rely-A-Cam-Gestures", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    return {};
  const QByteArray b(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
  LocalFree(out.pbData);
  return QString::fromLatin1(b.toBase64());
}

QString unprotect(const QString &stored) {
  if (stored.isEmpty()) return {};
  const QByteArray in = QByteArray::fromBase64(stored.toLatin1());
  DATA_BLOB a{DWORD(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data()))}, out{};
  if (!CryptUnprotectData(&a, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
  const QString s = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
  LocalFree(out.pbData);
  return s;
}

// The installer's output ticks (HKLM Software\Rely-A-Cam-Gestures), the defaults until the
// settings file has its own answer.
bool installerTick(const wchar_t *name, bool fallback) {
  DWORD v = 0, size = sizeof v;
  if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Rely-A-Cam-Gestures", name, RRF_RT_REG_DWORD, nullptr, &v, &size) !=
      ERROR_SUCCESS)
    return fallback;
  return v != 0;
}

QJsonObject places(const Settings::Places &p) { return {{"preview", p.preview}, {"output", p.output}}; }
Settings::Places places(const QJsonValue &v, Settings::Places def) {
  const QJsonObject o = v.toObject();
  return {o["preview"].toBool(def.preview), o["output"].toBool(def.output)};
}

}  // namespace

Settings Settings::load(const QString &path) {
  Settings s;
  s.outVirtualCamera = installerTick(L"OutVirtualCamera", true);
  s.outObsCamera = installerTick(L"OutObsCamera", true);
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return s;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  if (o["version"].toInt() != kVersion) return s;
  s.cameraId = o["camera"].toString();
  s.gesturesOn = o["gestures_on"].toBool(true);
  s.preferGpu = o["prefer_gpu"].toBool(true);
  s.outVirtualCamera = o["out_virtual_camera"].toBool(s.outVirtualCamera);
  s.outObsCamera = o["out_obs_camera"].toBool(s.outObsCamera);
  s.memeSound = o["meme_sound"].toString() == "calls" ? MemeSound::Calls : MemeSound::Speakers;
  s.callMic = o["call_mic"].toString();
  s.cameraOffPicture = o["camera_off_picture"].toString();
  const QString confirm = o["confirm_sign"].toString("Thumb_Up");
  for (int i = 1; i < int(vision::Sign::Count); ++i)
    if (confirm == vision::signId(vision::Sign(i))) s.confirmSign = vision::Sign(i);
  s.stopSign = o["stop_sign"].toString() == "crossed_arms" ? gestures::StopSign::CrossedArms
                                                            : gestures::StopSign::BothPalms;
  s.startWithWindows = o["start_with_windows"].toBool(false);
  s.grid = places(o["grid"], s.grid);
  s.detection = places(o["detection"], s.detection);
  s.obsPort = o["obs_port"].toInt(4455);
  s.obsPassword = unprotect(o["obs_password"].toString());
  return s;
}

bool Settings::save(const QString &path) const {
  QJsonObject o{{"version", kVersion},
                {"camera", cameraId},
                {"gestures_on", gesturesOn},
                {"prefer_gpu", preferGpu},
                {"out_virtual_camera", outVirtualCamera},
                {"out_obs_camera", outObsCamera},
                {"meme_sound", memeSound == MemeSound::Calls ? "calls" : "speakers"},
                {"call_mic", callMic},
                {"camera_off_picture", cameraOffPicture},
                {"confirm_sign", vision::signId(confirmSign)},
                {"stop_sign", stopSign == gestures::StopSign::CrossedArms ? "crossed_arms" : "both_palms"},
                {"start_with_windows", startWithWindows},
                {"grid", places(grid)},
                {"detection", places(detection)},
                {"obs_port", obsPort},
                {"obs_password", protect(obsPassword)}};
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
  return f.commit();
}

}  // namespace racg
