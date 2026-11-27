#include "app/controller.hpp"

#include "common/clock.hpp"
#include "common/paths.hpp"
#include "memes/itsgiving_fetch.hpp"
#include "output/obs_camera.hpp"
#include "output/win_camera.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTimer>

namespace racg::app {

using gestures::Command;
using Done = actions::Runner::Done;

namespace {
constexpr int kLayerMs = 33;        // the layer and the crop go to the plugin at 30 fps
constexpr int kLayerMax = 1280;     // the layer's long side (fits link::kOutSlotBytes)
const QString kWebcam = "webcam";

QSize layerSize(QSize out) {
  if (out.isEmpty()) out = {1280, 720};
  return out.width() <= kLayerMax && out.height() <= kLayerMax ? out : out.scaled(kLayerMax, kLayerMax, Qt::KeepAspectRatio);
}
}  // namespace

// Everything the window reads exists from here on; start() only starts the cameras and seeing.
Controller::Controller() {
  settings_ = Settings::load(paths::settingsFile());
  gestures_ = gestures::Library::load(paths::gesturesFile());
  memes_ = memes::Library::load(paths::memesFile());
  for (auto &m : memes_.memes) probeSound(m);   // memes added before videos were probed
  baseline_ = gestures::Baseline::load(paths::calibrationFile());
  addChannel(kWebcam, {}, false);
  applyEngineConfig();
  cameras_ = vision::Camera::list();

  connect(&link_, &link::Server::attached, this, &Controller::onAttached);
  connect(&link_, &link::Server::detached, this, &Controller::onDetached);
  connect(&link_, &link::Server::message, this, &Controller::onLinkMessage);
  connect(&link_, &link::Server::frame, this, &Controller::onLinkFrame);
  connect(&link_, &link::Server::upload, this, &Controller::onUpload);
  layerTimer_ = new QTimer(this);
  layerTimer_->setInterval(kLayerMs);
  connect(layerTimer_, &QTimer::timeout, this, &Controller::sendLayers);

  connect(&obsWs_, &obs::WebSocket::changed, this, [this] {
    if (!attached()) Q_EMIT statusChanged();
  });
  mixer_.onLink = [this](const float *lr, int frames) { link_.sendAudio(lr, frames); };
}

Controller::~Controller() { shutdown(); }

double Controller::now() const { return clock::now(); }

bool Controller::zoomed() const {
  const QRectF t = cur().framer.target();
  return t.width() < 0.98 || t.height() < 0.98;
}

void Controller::resetFraming() {
  cur().framer.reset();
  toast("The whole picture again");
}

Channel &Controller::addChannel(const QString &key, const QString &name, bool phone) {
  auto ch = std::make_unique<Channel>(gestures_);
  Channel &c = *ch;
  c.key = key;
  c.name = name;
  c.phone = phone;
  c.engine.setPaused(!settings_.gesturesOn);
  c.pipeline.setMirror(!phone);
  c.pipeline.setOutputOverlays(settings_.grid.output, settings_.detection.output, settings_.faceMap.output);
  c.framer.setMoves(moves());
  c.framer.setFollowSpan(settings_.followSpan);
  c.pipeline.setCropSource([&c](qint64 t) { return c.framer.crop(t / 1000.0); });
  c.pipeline.setMemeLayer([&c](QPainter &p, const output::FrameInfo &info, const output::DetectionState &det, qint64 t) {
    c.show.draw(p, info, det, t);
  });
  c.pipeline.onFrame = [this, &c] {
    // Coalesce: the preview repaints once per UI turn, however fast frames come.
    if (!c.framePosted.exchange(true))
      QMetaObject::invokeMethod(this, [this, key = c.key, ptr = &c] {
        auto it = channels_.find(key);
        if (it == channels_.end() || it->second.get() != ptr) return;
        ptr->framePosted = false;
        if (key == current_) Q_EMIT frame();
      }, Qt::QueuedConnection);
  };
  c.detector.setMirror(!phone);

  actions::Runner::Deps deps;
  deps.settings = &settings_;
  deps.pipeline = &c.pipeline;
  deps.memes = &memes_;
  deps.situation = [this, &c] {
    if (c.phone) return actions::Situation::Rely;
    return obsWs_.found() ? actions::Situation::OwnObs : actions::Situation::Own;
  };
  deps.paused = [&c] { return c.engine.paused(); };
  deps.setPaused = [this](bool p) { setGesturesOn(!p); };
  deps.overlaysChanged = [this] {
    for (auto &[k, ch] : channels_) ch->pipeline.setOutputOverlays(settings_.grid.output, settings_.detection.output, settings_.faceMap.output);
    settings_.save(paths::settingsFile());
    sendStatus();
    Q_EMIT statusChanged();
  };
  c.runner = std::make_unique<actions::Runner>(deps);
  connect(c.runner.get(), &actions::Runner::message, this, &Controller::toast);
  connect(c.runner.get(), &actions::Runner::forgotten, this, [&c](const QString &g) { c.engine.forget(g); });
  connect(c.runner.get(), &actions::Runner::changed, this, &Controller::statusChanged);
  addHandlers(c);
  {
    gestures::EngineConfig cfg;
    cfg.confirmSign = settings_.confirmSign;
    cfg.stopSign = settings_.stopSign;
    cfg.oneShot = [](const QString &a) { return actions::isOneShot(a); };
    c.engine.setConfig(cfg);
  }
  channels_[key] = std::move(ch);
  if (started_) startDetector(c);
  return c;
}

void Controller::startDetector(Channel &ch) {
  const QString key = ch.key;
  Channel *ptr = &ch;
  ch.detector.onReady = [this, key, ptr](bool ok, const QString &device, const QString &error) {
    QMetaObject::invokeMethod(this, [this, key, ptr, ok, device, error] {
      auto it = channels_.find(key);
      if (it == channels_.end() || it->second.get() != ptr) return;
      device_ = device;
      detectorError_ = error;
      if (!ok) toast("Gestures can't run: " + error);
      qInfo() << "detector" << key << (ok ? "ready on" : "failed:") << (ok ? device : error);
      sendStatus();
      Q_EMIT statusChanged();
    }, Qt::QueuedConnection);
  };
  ch.detector.onObservation = [this, key, ptr](std::shared_ptr<const vision::Observation> obs) {
    QMetaObject::invokeMethod(this, [this, key, ptr, obs] {
      auto it = channels_.find(key);
      if (it == channels_.end() || it->second.get() != ptr) return;
      onObservation(*ptr, obs);
    }, Qt::QueuedConnection);
  };
  ch.detector.start(paths::modelsDir(), settings_.preferGpu);
}

void Controller::start() {
  started_ = true;
  for (auto &[k, ch] : channels_) startDetector(*ch);
  applyOutputs();
  applySound();
  obsWs_.setTarget(settings_.obsPort, settings_.obsPassword);
  obsWs_.start();
  link_.listen();
  if (!attachLaunch_) {
    openCamera();
  } else {
    // Started by Rely-A-Cam: wait for it instead of grabbing the webcam (and its outputs) first.
    QTimer::singleShot(kAttachWaitMs, this, [this] {
      if (attached() || !started_) return;
      if (!(windowVisible && windowVisible())) {
        QCoreApplication::quit();
        return;
      }
      openCamera();
    });
  }
  if (!settings_.itsgivingFetched) fetchItsgiving();
}

void Controller::shutdown() {
  if (!started_) return;
  started_ = false;
  layerTimer_->stop();
  link_.bye("quit");
  for (auto &[k, ch] : channels_) ch->runner->endAll();
  mixer_.stopAll();
  camera_.close();
  for (auto &[k, ch] : channels_) ch->detector.stop();
  obsWs_.stop();
  gestures_.save(paths::gesturesFile());
  memes_.save(paths::memesFile());
  settings_.save(paths::settingsFile());
}

void Controller::applyEngineConfig() {
  gestures::EngineConfig c;
  c.confirmSign = settings_.confirmSign;
  c.stopSign = settings_.stopSign;
  c.oneShot = [](const QString &a) { return actions::isOneShot(a); };
  for (auto &[k, ch] : channels_) ch->engine.setConfig(c);
}

// ---- action handlers ------------------------------------------------------------------------------

void Controller::addHandlers(Channel &ch) {
  actions::Runner &r = *ch.runner;
  Channel *c = &ch;

  r.handle("meme", [this, c](const QString &id) {
    Done d;
    const memes::Meme *m = memes_.find(id);
    if (!m) {
      d.ok = false;
      d.label = "That meme was removed";
      return d;
    }
    const QString err = c->show.start(*m, clock::nowMs());
    if (!err.isEmpty()) {
      d.ok = false;
      d.label = err;
      return d;
    }
    const QString key = c->key + "/" + id;
    if (m->hasSound())
      if (auto clip = clipFor(*m)) mixer_.play(key, clip, m->volume / 100.f, m->kind != memes::Kind::Picture);
    d.label = m->name();
    d.end = [this, c, id, key] {
      c->show.stop(id);
      mixer_.stop(key);
    };
    return d;
  });

  // Zoom steps: the phone's own zoom when attached, the webcam's own zoom when it has one, else a
  // crop. One-shot: undo steps back.
  auto zoomStep = [this, c](int delta) {
    return [this, c, delta](const QString &) {
      Done d;
      auto step = [this, c](int dl) {
        if (c->phone) {
          sendAction(*c, "zoom.step", dl > 0 ? "+1" : "-1");
          return;
        }
        const int i = c->framer.stepPreset(dl);
        const auto range = camera_.range(vision::Camera::Zoom);
        if (range.ok && range.max > range.min) {
          const int n = int(std::size(framing::kPresets)) - 1;
          camera_.set(vision::Camera::Zoom, range.min + (range.max - range.min) * i / std::max(1, n));
          c->framer.setPreset(0);   // the lens zooms; no crop on top
          hwZoom_ = i;
        }
      };
      step(delta);
      d.label = delta > 0 ? "zoom in" : "zoom out";
      d.revert = [step, delta] { step(-delta); };
      return d;
    };
  };
  r.handle("zoom.in", zoomStep(+1));
  r.handle("zoom.out", zoomStep(-1));

  // Follow face's gesture is a chord: while it's held a pinch zoom from the other hand sets how
  // close follow is; letting go toggles follow only if nothing was set (framing/framer.hpp).
  r.handle("follow.face", [c](const QString &) {
    Done d;
    c->framer.beginChord();
    d.end = [c] { c->framer.endChord(); };
    return d;
  });
  r.handle("obs.scene.next", [this, c](const QString &) {
    Done d;
    const QStringList scenes = obsScenes();
    if (scenes.isEmpty()) {
      d.ok = false;
      d.label = "OBS has no scenes";
      return d;
    }
    const QString before = obsScene();
    const QString next = scenes[(scenes.indexOf(before) + 1) % scenes.size()];
    obsDo(*c, "obs.scene", next);
    d.label = "scene " + next;
    if (!before.isEmpty() && before != next) d.revert = [this, c, before] { obsDo(*c, "obs.scene", before); };
    return d;
  });

  // OBS: through the plugin when attached, its WebSocket on its own.
  r.handle("obs.scene", [this, c](const QString &scene) {
    Done d;
    const QString before = obsScene();
    obsDo(*c, "obs.scene", scene);
    d.label = "scene " + scene;
    if (!before.isEmpty() && before != scene) d.revert = [this, c, before] { obsDo(*c, "obs.scene", before); };
    return d;
  });
  auto output = [this, c](const QString &id, const QString &startWord, const QString &stopWord) {
    return [this, c, id, startWord, stopWord](const QString &) {
      Done d;
      const bool was = id == "obs.record" ? obsRecording() : obsStreaming();
      obsDo(*c, id, was ? "off" : "on");
      d.label = was ? stopWord : startWord;
      d.end = [this, c, id, was] { obsDo(*c, id, was ? "on" : "off"); };
      return d;
    };
  };
  r.handle("obs.record", output("obs.record", "record", "stop recording"));
  r.handle("obs.stream", output("obs.stream", "go live", "stop streaming"));
  r.handle("obs.hotkey", [this, c](const QString &name) {
    Done d;
    obsDo(*c, "obs.hotkey", name);
    d.label.clear();   // a hotkey can't be taken back
    return d;
  });

  // The phone (attached only; the registry greys these out on their own).
  auto phoneToggle = [this, c](const QString &id, const QString &field, const QString &label) {
    return [this, c, id, field, label](const QString &) {
      Done d;
      const bool was = c->state[field].toBool();
      sendAction(*c, id, was ? "off" : "on");
      d.label = label;
      d.end = [this, c, id, was] { sendAction(*c, id, was ? "on" : "off"); };
      return d;
    };
  };
  r.handle("phone.torch", phoneToggle("phone.torch", "torch", "torch"));
  r.handle("phone.mic", phoneToggle("phone.mic", "muted", "mute mic"));
  r.handle("phone.miconly", phoneToggle("phone.miconly", "micOnly", "mic only"));
  r.handle("phone.switch", [this, c](const QString &) {
    Done d;
    const bool front = c->state["front"].toBool();
    sendAction(*c, "phone.switch", front ? "back" : "front");
    d.label = "switch camera";
    d.end = [this, c, front] { sendAction(*c, "phone.switch", front ? "front" : "back"); };
    return d;
  });

  // Attached, a snapshot is the phone's full-size photo, saved by Rely-A-Cam like its own button.
  if (ch.phone)
    r.handle("snapshot", [this, c](const QString &) {
      Done d;
      sendAction(*c, "snapshot", {});
      d.label.clear();
      return d;
    });
}

std::shared_ptr<const audio::Clip> Controller::clipFor(const memes::Meme &m) {
  const QString file = QDir(paths::mediaDir()).filePath(m.sound.isEmpty() ? m.file : m.sound);
  auto it = clips_.find(file);
  if (it != clips_.end()) return it->second;
  auto clip = audio::load(file);
  if (!clip) toast(m.name() + ": its sound can't be read");
  clips_[file] = clip;
  return clip;
}

// ---- camera --------------------------------------------------------------------------------------

void Controller::refreshCameras() {
  cameras_ = vision::Camera::list();
  Q_EMIT statusChanged();
}

void Controller::setStill(const QString &file) {
  // Developer aid: a picture plays as the camera at 30 fps, so the overlay and the gestures can be
  // checked without a webcam (`--still <file>`). Never offered in the window.
  QImage img(file);
  if (img.isNull()) {
    qWarning() << "--still: can't read" << file;
    return;
  }
  still_ = img.convertToFormat(QImage::Format_RGB32).mirrored(true, false);   // a camera frame isn't mirrored yet
  auto *t = new QTimer(this);
  connect(t, &QTimer::timeout, this, [this] {
    if (attached()) return;
    Channel &w = *channels_.at(kWebcam);
    const qint64 now = clock::nowMs();
    w.detector.submit(still_, now);
    w.pipeline.process(still_, now);
  });
  t->start(33);
}

void Controller::openCamera() {
  if (!still_.isNull() || attached()) return;
  camera_.close();
  cameraError_.clear();
  hwZoom_ = 0;
  if (cameras_.isEmpty()) {
    cameraError_ = "No camera found. Plug one in, then pick it in the Camera list.";
    Q_EMIT statusChanged();
    return;
  }
  QString id = cameras_.first().id;
  for (const auto &c : cameras_)
    if (c.id == settings_.cameraId) id = c.id;
  Channel &w = *channels_.at(kWebcam);
  camera_.onFrame = [&w](const QImage &img, qint64 t) {
    w.detector.submit(img, t);
    w.pipeline.process(img, t);
  };
  camera_.onLost = [this](const QString &why) {
    QMetaObject::invokeMethod(this, [this, why] {
      cameraError_ = why;
      toast(why);
      Q_EMIT statusChanged();
    }, Qt::QueuedConnection);
  };
  if (!camera_.open(id)) cameraError_ = camera_.error();
  else qInfo() << "camera" << cameraName() << camera_.size();
  w.name = cameraName();
  w.detector.resetTracking();
  applyOutputs();   // the loop guard depends on which camera this is
  Q_EMIT statusChanged();
}

void Controller::setCamera(const QString &id) {
  if (id == settings_.cameraId && camera_.isOpen()) return;
  settings_.cameraId = id;
  settings_.save(paths::settingsFile());
  openCamera();
}

QString Controller::cameraName() const {
  if (attached()) return cur().name;
  for (const auto &c : cameras_)
    if (c.id == settings_.cameraId) return c.name;
  return cameras_.isEmpty() ? QString() : cameras_.first().name;
}

void Controller::applyOutputs() {
  Channel &w = *channels_.at(kWebcam);
  const QString cam = attached() ? QString() : cameraName();
  // Never write into the camera being read: that's a picture of itself, forever.
  const bool obsLoop = cam.contains("OBS Virtual Camera", Qt::CaseInsensitive);
  const bool ownLoop = cam.contains(output::WinCameraSink::kName, Qt::CaseInsensitive);
  // The outputs are the webcam's, on its own: attached, the phones go to OBS and the webcam is shut.
  const bool alone = started_ && !attached();
  const bool wantObs = alone && settings_.outObsCamera && !obsLoop;
  const bool wantWin = alone && settings_.outVirtualCamera && !ownLoop;
  if (wantObs && !obsCam_) {
    obsCam_ = std::make_shared<output::ObsCameraSink>();
    w.pipeline.addSink(obsCam_);
  } else if (!wantObs && obsCam_) {
    w.pipeline.removeSink(obsCam_.get());
    obsCam_.reset();
  }
  if (wantWin && !winCam_) {
    winCam_ = std::make_shared<output::WinCameraSink>();
    w.pipeline.addSink(winCam_);
  } else if (!wantWin && winCam_) {
    w.pipeline.removeSink(winCam_.get());
    winCam_.reset();
  }
  if (alone && obsLoop && settings_.outObsCamera) toast("Reading from OBS Virtual Camera, so this app doesn't also write to it");
}

void Controller::applySound() {
  if (attached()) mixer_.setRoute(audio::Mixer::Route::Link);
  else
    mixer_.setRoute(settings_.memeSound == Settings::MemeSound::Calls ? audio::Mixer::Route::Calls : audio::Mixer::Route::Speakers,
                    settings_.callMic);
}

// ---- status --------------------------------------------------------------------------------------

actions::Situation Controller::situation() const {
  if (attached() && cur().phone) return actions::Situation::Rely;
  if (obsFound()) return actions::Situation::OwnObs;
  return actions::Situation::Own;
}

QList<std::pair<QString, QString>> Controller::phones() const {
  QList<std::pair<QString, QString>> out;
  for (const auto &[k, ch] : channels_)
    if (ch->phone) out.push_back({k, ch->name});
  return out;
}

void Controller::showPhone(const QString &device) {
  if (!channels_.count(device) || device == current_) return;
  current_ = device;
  Q_EMIT statusChanged();
  Q_EMIT libraryChanged();
}

QStringList Controller::obsScenes() const {
  if (attached()) {
    QStringList s;
    for (const auto &v : obsFromPlugin_["scenes"].toArray()) s << v.toString();
    return s;
  }
  return obsWs_.scenes();
}

QStringList Controller::obsHotkeys() const {
  if (attached()) {
    QStringList s;
    for (const auto &v : obsFromPlugin_["hotkeys"].toArray()) s << v.toString();
    return s;
  }
  return obsWs_.hotkeys();
}

bool Controller::obsFound() const { return attached() || obsWs_.found(); }
bool Controller::obsNeedsPassword() const { return !attached() && obsWs_.needsPassword(); }
bool Controller::obsRecording() const { return attached() ? obsFromPlugin_["recording"].toBool() : obsWs_.recording(); }
bool Controller::obsStreaming() const { return attached() ? obsFromPlugin_["streaming"].toBool() : obsWs_.streaming(); }
QString Controller::obsScene() const { return attached() ? obsFromPlugin_["scene"].toString() : obsWs_.scene(); }

void Controller::obsDo(const Channel &ch, const QString &id, const QString &arg) {
  if (attached()) {
    sendAction(ch, id, arg);
    return;
  }
  if (id == "obs.scene") obsWs_.setScene(arg);
  else if (id == "obs.record") obsWs_.setRecording(arg == "on");
  else if (id == "obs.stream") obsWs_.setStreaming(arg == "on");
  else if (id == "obs.hotkey") obsWs_.triggerHotkey(arg);
}

QString Controller::detectorInfo() const {
  if (!detectorError_.isEmpty()) return "Detection stopped: " + detectorError_;
  if (device_.isEmpty()) return "Loading the models...";
  const auto &o = cur().obs;
  const QString size = o ? QString("%1x%2").arg(o->width).arg(o->height) : QString("640x360");
  return QString("15 fps detection · %1 · %2").arg(size, device_);
}

QString Controller::outputsLabel() const {
  QStringList on;
  auto add = [&on](const QString &name, const std::shared_ptr<output::Sink> &s) {
    if (!s) return;
    const QString e = s->error();
    on << (e.isEmpty() ? name : name + " (" + e + ")");
  };
  add("Virtual camera", winCam_);
  add("OBS Virtual Camera", obsCam_);
  return on.isEmpty() ? QString("No outputs") : on.join(" + ");
}

void Controller::toast(const QString &text) {
  qInfo() << "note:" << text;
  Q_EMIT toastShown(text);
}

// ---- observations --------------------------------------------------------------------------------

void Controller::onObservation(Channel &ch, std::shared_ptr<const vision::Observation> obs) {
  ch.obs = obs;
  const double t = obs->timeMs / 1000.0;
  const bool shown = ch.key == current_;
  const bool busy = shown && (calibrator_.running() || recorder_.phase() != gestures::Recorder::Phase::Idle ||
                              tester_.mode() != gestures::Tester::Mode::Off);

  if (shown && calibrator_.running() && calibrator_.add(*obs, t)) {
    if (calibrator_.ok()) {
      baseline_ = *calibrator_.result;
      baseline_.save(paths::calibrationFile());
      toast(calibrator_.warnings.empty() ? QString("Calibrated on %1 frames").arg(baseline_.samples)
                                         : "Calibrated, but: " + calibrator_.warnings.front());
    } else {
      toast(calibrator_.error);
    }
    Q_EMIT calibrated();
  }

  if (shown && recorder_.phase() != gestures::Recorder::Phase::Idle && recorder_.add(*obs, baseline_, t)) {
    gestures::GestureDef *g = gestures_.find(recorder_.gestureId);
    if (!recorder_.take) {
      toast(recorder_.error);
    } else if (g) {
      gestures::Take take = *recorder_.take;
      const QString file = QString("%1_%2.jpg").arg(QString(g->id).replace(':', '_')).arg(QDateTime::currentMSecsSinceEpoch());
      recorder_.picture.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation)
          .save(QDir(paths::takesDir()).filePath(file), "JPG", 85);
      take.thumb = file;
      g->takes.push_back(take);
      if (g->takes.size() == 1) g->parts = gestures::defaultParts(g->takes, baseline_);
      gestures_.save(paths::gesturesFile());
      toast(QString("Take %1 saved").arg(g->takes.size()));
      Q_EMIT takeRecorded(g->id);
      Q_EMIT libraryChanged();
      sendLibrary();
    }
  }

  const auto cmds = ch.engine.update(*obs, baseline_, t, busy);
  if (shown && tester_.mode() != gestures::Tester::Mode::Off)
    if (const auto *g = gestures_.find(tester_.gestureId())) tester_.add(*g, ch.engine.state(g->id), t);
  runCommands(ch, cmds);
  ch.framer.update(*obs, t);
  // The stop sign also puts the framing back: the whole picture, follow off.
  if (ch.engine.stopProgress() >= 1) {
    if (!ch.stopped) ch.framer.reset();
    ch.stopped = true;
  } else {
    ch.stopped = false;
  }
  if (ch.framer.takeSpanChanged()) {   // a pinch set how close follow is: every camera, saved
    settings_.followSpan = ch.framer.followSpan();
    for (auto &[k, other] : channels_)
      if (other.get() != &ch) other->framer.setFollowSpan(settings_.followSpan);
    if (!spanSave_) {
      spanSave_ = true;
      QTimer::singleShot(1500, this, [this] {
        spanSave_ = false;
        settings_.save(paths::settingsFile());
      });
    }
  }

  // The detection overlay: what each visible gesture is doing, anchored on its hand or the face.
  output::DetectionState ds;
  ds.obs = obs;
  ds.stop = ch.engine.stopProgress();
  QJsonArray detG;
  for (const auto &[g, st] : ch.engine.visible()) {
    output::DetectionState::Gesture v;
    v.name = g->name;
    v.armed = st.armed;
    v.waiting = st.waitingConfirm;
    v.confirmName = vision::signLabel(settings_.confirmSign);
    v.anchor = {obs->width / 2.f, obs->height * 0.6f};
    bool placed = false;
    for (size_t i = 0; i < obs->hands.size() && !placed; ++i)
      if (st.hands & (1u << i)) {
        v.anchor = obs->hands[i].lm[9].xy();
        placed = true;
      }
    if (!placed && obs->face) v.anchor = obs->face->lm[152].xy();
    if (ch.phone)
      detG.append(QJsonObject{{"n", v.name},
                              {"s", v.waiting ? "waiting" : v.armed >= 1 ? "armed" : ""},
                              {"a", double(v.armed)},
                              {"c", v.confirmName},
                              {"x", v.anchor.x / std::max(1, obs->width)},
                              {"y", v.anchor.y / std::max(1, obs->height)}});
    ds.gestures.push_back(v);
  }
  const double W = std::max(1, obs->width), H = std::max(1, obs->height);
  auto label = [&](const output::DetectionState::Gesture &v) {
    return QJsonObject{{"n", v.name},
                       {"s", v.waiting ? "waiting" : v.armed >= 1 ? "armed" : ""},
                       {"a", double(v.armed)},
                       {"c", v.confirmName},
                       {"x", v.anchor.x / W},
                       {"y", v.anchor.y / H}};
  };
  // The pinch moves: a ring while a grab arms, then the zoom.
  for (const auto &mk : ch.framer.marks()) {
    output::DetectionState::Gesture v;
    v.name = mk.text;
    v.armed = mk.ring;
    v.anchor = mk.at;
    if (ch.phone) detG.append(label(v));
    ds.gestures.push_back(v);
  }
  // The face map: the face's lines, and the face gesture you're making (the best face-only
  // gesture, named with its match) unless it's already firing.
  QJsonArray faceG;
  if ((settings_.faceMap.preview || settings_.faceMap.output) && obs->face) {
    ds.face = output::faceContours(*obs->face);
    const gestures::GestureDef *best = nullptr;
    float bestPct = 0;
    for (const auto &g : gestures_.gestures) {
      if (!g.on || !g.parts.face || g.parts.leftHand || g.parts.rightHand) continue;
      const auto *st = ch.engine.state(g.id);
      if (st && st->pct > bestPct) {
        bestPct = st->pct;
        best = &g;
      }
    }
    bool firing = false;
    for (const auto &v : ds.gestures) firing |= best && v.name == best->name;
    if (best && bestPct >= 20 && !firing) {
      output::DetectionState::Gesture v;
      v.face = true;
      v.name = QString("%1 %2%").arg(best->name).arg(int(std::min(bestPct, 100.f)));
      v.armed = std::clamp(bestPct / float(std::max(1, best->sens)), 0.f, 1.f);
      v.anchor = obs->face->lm[152].xy();
      faceG.append(label(v));
      ds.gestures.push_back(v);
    }
  }
  ch.det = ds;
  ch.pipeline.setDetection(ds);

  // Attached with the dock's detection overlay or face map on: what to draw, as fractions.
  if (ch.phone && attached() && (settings_.detection.preview || settings_.faceMap.preview)) {
    auto round3 = [](double v) { return std::round(v * 1000) / 1000; };
    QJsonArray hands, g;
    if (settings_.detection.preview) {
      for (const auto &h : obs->hands) {
        QJsonArray pts;
        for (const auto &p : h.lm) {
          pts.append(round3(p.x / W));
          pts.append(round3(p.y / H));
        }
        hands.append(pts);
      }
      g = detG;
    }
    QJsonObject det{{"device", ch.key}, {"hands", hands}, {"stop", settings_.detection.preview ? ds.stop : 0.f}};
    if (settings_.faceMap.preview) {
      for (const auto &v : faceG) g.append(v);
      QJsonArray lines;
      for (const auto &l : ds.face) {
        QJsonArray pts;
        for (const Vec2 &v : l) {
          pts.append(round3(v.x / W));
          pts.append(round3(v.y / H));
        }
        lines.append(pts);
      }
      det["face"] = lines;
    }
    det["g"] = g;
    link_.send(link::Msg::Det, det);
  }
  if (shown) Q_EMIT observed();
}

void Controller::runCommands(Channel &ch, const std::vector<Command> &cmds) {
  for (const auto &c : cmds) {
    qInfo() << ch.key << (c.type == Command::Start ? "start" : "end") << c.gesture << c.action << c.reason;
    if (c.type == Command::Start) ch.runner->start(c);
    else ch.runner->end(c);
    if (ch.phone && attached())
      if (const auto *g = gestures_.find(c.gesture))
        link_.send(link::Msg::Event, {{"device", ch.key}, {"gesture", c.gesture}, {"name", g->name},
                                      {"op", c.type == Command::Start ? "start" : "end"}});
  }
}

// ---- the link to Rely-A-Cam ----------------------------------------------------------------------

void Controller::onAttached() {
  qInfo() << "attached to Rely-A-Cam" << link_.pluginVersion();
  // The webcam steps aside: its actions end and the camera is let go for other apps.
  Channel &w = *channels_.at(kWebcam);
  w.runner->endAll();
  w.show.stopAll();
  camera_.close();
  applyOutputs();   // lets go of OBS Virtual Camera and our own camera
  applySound();
  layerTimer_->start();
  sendStatus();
  sendLibrary();
  toast("Attached to Rely-A-Cam in OBS");
  Q_EMIT statusChanged();
}

void Controller::onDetached() {
  qInfo() << "detached from Rely-A-Cam";
  layerTimer_->stop();
  mixer_.stopAll();
  for (auto it = channels_.begin(); it != channels_.end();) {
    if (it->second->phone) {
      it->second->runner->endAll();
      it->second->detector.stop();
      it = channels_.erase(it);
    } else {
      ++it;
    }
  }
  current_ = kWebcam;
  obsFromPlugin_ = {};
  // Started by the plugin and nobody opened the window: nothing left to do.
  if (attachLaunch_ && !(windowVisible && windowVisible())) {
    QCoreApplication::quit();
    return;
  }
  applyOutputs();
  applySound();
  openCamera();
  toast("Rely-A-Cam let go: back on the webcam");
  Q_EMIT statusChanged();
  Q_EMIT libraryChanged();
}

void Controller::onLinkMessage(link::Msg type, const QJsonObject &j) {
  switch (type) {
    case link::Msg::Phones: {
      // Phones with gestures on: one channel each; gone ones end their actions.
      QStringList keep;
      for (const auto &v : j["phones"].toArray()) {
        const QJsonObject p = v.toObject();
        const QString dev = p["device"].toString();
        if (dev.isEmpty()) continue;
        keep << dev;
        auto it = channels_.find(dev);
        if (it == channels_.end()) addChannel(dev, p["name"].toString(), true);
        else it->second->name = p["name"].toString();
      }
      for (auto it = channels_.begin(); it != channels_.end();) {
        if (it->second->phone && !keep.contains(it->first)) {
          if (it->first == current_) current_ = kWebcam;
          it->second->runner->endAll();
          it->second->detector.stop();
          it = channels_.erase(it);
        } else {
          ++it;
        }
      }
      if (!keep.isEmpty() && !keep.contains(current_)) current_ = keep.first();
      Q_EMIT statusChanged();
      Q_EMIT libraryChanged();
      break;
    }
    case link::Msg::Phone: {
      auto it = channels_.find(j["device"].toString());
      if (it == channels_.end()) break;
      it->second->state = j["state"].toObject();
      break;
    }
    case link::Msg::Obs:
      obsFromPlugin_ = j;
      Q_EMIT statusChanged();
      break;
    case link::Msg::Cmd:
      if (j.contains("openEditor")) {
        showPhone(j["openEditor"].toString());
        Q_EMIT showWindow();
      }
      if (j.contains("gestures")) setGesturesOn(j["gestures"].toBool());
      if (j.contains("overlay")) {
        const QJsonObject o = j["overlay"].toObject();
        const QString which = o["which"].toString();
        Settings::Places &p = which == "grid" ? settings_.grid : which == "face" ? settings_.faceMap : settings_.detection;
        (o["place"].toString() == "output" ? p.output : p.preview) = o["on"].toBool();
        settingsEdited();
      }
      break;
    case link::Msg::LibSet: {
      if (j.contains("control")) {
        const QJsonObject k = j["control"].toObject();
        setControlOn(k["id"].toString(), k["on"].toBool());
      }
      if (j.contains("gesture")) {
        const QJsonObject g = j["gesture"].toObject();
        if (auto *def = gestures_.find(g["id"].toString())) {
          if (g.contains("on")) def->on = g["on"].toBool();
          gesturesEdited();
        }
      }
      if (j.contains("meme")) {
        const QJsonObject m = j["meme"].toObject();
        if (auto *def = memes_.find(m["id"].toString())) {
          const QString place = m["place"].toString();
          if (place == "head") def->place = memes::Place::Head;
          else if (place == "full") def->place = memes::Place::Full;
          else if (place == "spot") def->place = memes::Place::Spot;
          if (m.contains("volume")) def->volume = std::clamp(m["volume"].toInt(), 0, 100);
          memesEdited();
        }
      }
      if (j.contains("removeMeme")) {
        memes_.remove(j["removeMeme"].toString());
        memesEdited();
      }
      // A take from the phone's Library: recorded from that phone's camera, shown in the window
      // so the 3-2-1 and the hold are visible on the PC as well.
      const QString from = j["device"].toString();
      QString take = j["record"].toString();
      if (j["newGesture"].toBool()) take = newGesture();
      if (!take.isEmpty() && gestures_.find(take)) {
        if (!from.isEmpty() && channels_.count(from)) showPhone(from);
        recordTake(take);
        Q_EMIT showWindow();
      }
      break;
    }
    default: break;
  }
}

void Controller::onLinkFrame(const QString &device, const QImage &picture, QSize out) {
  auto it = channels_.find(device);
  if (it == channels_.end()) return;
  Channel &ch = *it->second;
  ch.outSize = out;
  const qint64 t = clock::nowMs();
  ch.detector.submit(picture, t);
  // Only the phone the window shows is composed for the preview.
  if (device == current_) ch.pipeline.process(picture, t);
}

void Controller::onUpload(const QString &name, const QByteArray &bytes) {
  QTemporaryDir dir;
  const QString file = dir.filePath(QFileInfo(name).fileName());
  QFile f(file);
  if (!dir.isValid() || !f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
    toast("A meme from the phone couldn't be saved");
    return;
  }
  f.close();
  memes::Meme *added = memes_.add(file);
  if (added) probeSound(*added);
  const bool ok = added != nullptr;
  memesEdited();
  toast(ok ? "Added " + QFileInfo(name).fileName() + " from the phone" : QFileInfo(name).fileName() + " isn't a picture, GIF or video");
}

void Controller::sendLayers() {
  const qint64 t = clock::nowMs();
  for (auto &[k, chp] : channels_) {
    Channel &ch = *chp;
    if (!ch.phone) continue;
    const bool needed = ch.show.any() || settings_.grid.output ||
                        ((settings_.detection.output || settings_.faceMap.output) && ch.obs) ||
                        ch.pipeline.cameraOff();
    if (needed) {
      QRectF crop;
      bool any = false;
      const QImage img = ch.pipeline.layer(layerSize(ch.outSize), t, &crop, &any);
      link_.sendLayer(k, img, false, crop);
      ch.layerEmpty = false;
      ch.lastCrop = crop;
    } else {
      const QRectF crop = ch.framer.crop(t / 1000.0);
      if (!ch.layerEmpty || crop != ch.lastCrop) link_.sendLayer(k, {}, true, crop);
      ch.layerEmpty = true;
      ch.lastCrop = crop;
    }
  }
}

void Controller::sendStatus() {
  if (!attached()) return;
  link_.send(link::Msg::Status,
             {{"version", QCoreApplication::applicationVersion()},
              {"ready", !device_.isEmpty() && detectorError_.isEmpty()},
              {"device", device_},
              {"error", detectorError_},
              {"gestures", settings_.gesturesOn},
              {"overlay", QJsonObject{{"grid", QJsonObject{{"dock", settings_.grid.preview}, {"output", settings_.grid.output}}},
                                      {"detection", QJsonObject{{"dock", settings_.detection.preview},
                                                                {"output", settings_.detection.output}}},
                                      {"face", QJsonObject{{"dock", settings_.faceMap.preview},
                                                           {"output", settings_.faceMap.output}}}}}});
}

void Controller::sendLibrary() {
  if (!attached()) return;
  QJsonArray gs, ms;
  for (const auto &g : gestures_.gestures) {
    const auto inf = actions::info(g.action, actions::Situation::Rely, memes_);
    QJsonObject o{{"id", g.id},
                  {"name", g.name},
                  {"on", g.on},
                  {"mine", g.kind == gestures::Kind::Mine},
                  {"action", g.action.isEmpty() ? QString() : inf.label}};
    if (const auto *k = gestures_.controls.byTrigger(g.id)) o["control"] = gestures::controlLabel(k->id);
    gs.append(o);
  }
  // Controls, for the phone's Library sheet: on or off there; gestures are picked on the PC.
  static const char *kGroup[] = {"Framing", "Camera and mic", "OBS", "Always"};
  QJsonArray cs;
  for (const auto &k : gestures_.controls.list) {
    const auto *spec = gestures::controlSpec(k.id);
    if (!spec) continue;
    const auto *tg = gestures_.find(k.trigger);
    cs.append(QJsonObject{{"id", k.id},
                          {"label", gestures::controlLabel(k.id)},
                          {"group", kGroup[int(spec->group)]},
                          {"on", k.on},
                          {"move", gestures::isMove(k.id)},
                          {"how", spec->how},
                          {"gesture", tg ? tg->icon + " " + tg->name : QString()}});
  }
  for (const auto &m : memes_.memes)
    ms.append(QJsonObject{{"id", m.id},
                          {"name", m.name()},
                          {"kind", m.kindLabel()},
                          {"place", m.place == memes::Place::Head ? "head" : m.place == memes::Place::Full ? "full" : "spot"},
                          {"volume", m.volume},
                          {"sound", m.hasSound()}});
  link_.send(link::Msg::Lib, {{"gestures", gs}, {"memes", ms}, {"controls", cs}});
}

void Controller::sendAction(const Channel &ch, const QString &id, const QString &arg) {
  if (!attached()) {
    toast("That needs Rely-A-Cam");
    return;
  }
  link_.send(link::Msg::Action, {{"device", ch.key}, {"id", id}, {"arg", arg}});
}

// ---- editing -------------------------------------------------------------------------------------

void Controller::setGesturesOn(bool on) {
  settings_.gesturesOn = on;
  for (auto &[k, ch] : channels_) ch->engine.setPaused(!on);
  settings_.save(paths::settingsFile());
  sendStatus();
  Q_EMIT statusChanged();
}

QString Controller::newGesture() {
  int n = 1;
  for (const auto &g : gestures_.gestures) n += g.kind == gestures::Kind::Mine;
  auto &g = gestures_.addMine(QString("Gesture %1").arg(n));
  gestures_.save(paths::gesturesFile());
  Q_EMIT libraryChanged();
  sendLibrary();
  return g.id;
}

void Controller::deleteGesture(const QString &id) {
  const gestures::GestureDef *g = gestures_.find(id);
  if (!g || g->kind != gestures::Kind::Mine) return;
  for (auto &[k, ch] : channels_) {
    ch->runner->end({Command::End, id, g->action, "deleted"});
    ch->engine.forget(id);
  }
  for (const auto &t : g->takes)
    if (!t.thumb.isEmpty()) QFile::remove(QDir(paths::takesDir()).filePath(t.thumb));
  gestures_.remove(id);
  gestures_.save(paths::gesturesFile());
  Q_EMIT libraryChanged();
  sendLibrary();
}

void Controller::deleteTake(const QString &gestureId, int index) {
  gestures::GestureDef *g = gestures_.find(gestureId);
  if (!g || index < 0 || index >= int(g->takes.size())) return;
  if (!g->takes[index].thumb.isEmpty()) QFile::remove(QDir(paths::takesDir()).filePath(g->takes[index].thumb));
  g->takes.erase(g->takes.begin() + index);
  gestures_.save(paths::gesturesFile());
  Q_EMIT libraryChanged();
}

QString Controller::takePicture(const QString &file) const { return QDir(paths::takesDir()).filePath(file); }

void Controller::recordTake(const QString &gestureId) {
  tester_.stop();
  recorder_.start(gestureId, now());
  Q_EMIT statusChanged();
}

void Controller::cancelRecording() {
  recorder_.cancel();
  Q_EMIT statusChanged();
}

void Controller::testLive(const QString &gestureId) {
  tester_.startLive(gestureId);
  Q_EMIT statusChanged();
}

void Controller::misfireCheck(const QString &gestureId) {
  tester_.startMisfire(gestureId, now());
  Q_EMIT statusChanged();
}

void Controller::stopTest() {
  tester_.stop();
  Q_EMIT statusChanged();
}

void Controller::calibrate() {
  calibrator_.start(now());
  toast("Calibrating: hold a bored face for 7 s");
  Q_EMIT statusChanged();
}

void Controller::undo() { cur().runner->undo(); }

void Controller::gesturesEdited() {
  gestures_.applyControls();
  for (auto &[k, ch] : channels_) ch->framer.setMoves(moves());
  gestures_.save(paths::gesturesFile());
  Q_EMIT libraryChanged();
  sendLibrary();
}

framing::Framer::Moves Controller::moves() const {
  const auto &k = gestures_.controls;
  return {k.moveOn("pinch.zoom"), k.moveOn("zoom.box"), k.moveOn("zoom.hold")};
}

void Controller::setControlOn(const QString &id, bool on) {
  const QString off = gestures_.controls.setOn(id, on);
  if (id == "follow.face" && !on)
    for (auto &[k, ch] : channels_) ch->framer.setFollow(false);
  gesturesEdited();
  if (!off.isEmpty())
    toast(QString("%1 is on, so %2 is off: they read the same move").arg(gestures::controlLabel(id), off));
}

void Controller::setControlTrigger(const QString &id, const QString &gestureId) {
  gestures_.controls.setTrigger(id, gestureId);
  gesturesEdited();
}

void Controller::addControl(const QString &id) {
  gestures_.controls.add(id);
  gesturesEdited();
}

void Controller::removeControl(const QString &id) {
  gestures_.controls.remove(id);
  gesturesEdited();
}

void Controller::memesEdited() {
  memes_.save(paths::memesFile());
  for (auto &[k, ch] : channels_) ch->show.refresh(memes_);
  clips_.clear();   // an attached sound may have changed
  Q_EMIT libraryChanged();
  sendLibrary();
}

void Controller::settingsEdited() {
  settings_.save(paths::settingsFile());
  applyEngineConfig();
  for (auto &[k, ch] : channels_) ch->pipeline.setOutputOverlays(settings_.grid.output, settings_.detection.output, settings_.faceMap.output);
  applyOutputs();
  applySound();
  obsWs_.setTarget(settings_.obsPort, settings_.obsPassword);
  sendStatus();
  Q_EMIT statusChanged();
}

void Controller::addMemes(const QStringList &files) {
  int added = 0;
  for (const auto &f : files)
    if (memes::Meme *m = memes_.add(f)) {
      probeSound(*m);
      ++added;
    }
  memesEdited();
  toast(added == files.size() ? QString("Added %1 meme%2").arg(added).arg(added == 1 ? "" : "s")
                              : QString("Added %1 of %2: the others aren't pictures, GIFs or videos").arg(added).arg(files.size()));
}

int Controller::importItsgiving(const QString &folder, bool downloaded) {
  const QStringList done = memes_.importItsgiving(folder);
  // Each reaction that has no action yet gets its own picture, so the 14 work like itsgiving.
  for (const QString &pose : done)
    if (auto *g = gestures_.find("ready:" + pose); g && g->action.isEmpty()) {
      g->action = "meme:" + pose;
      g->ends = gestures::Ends::Held;
    }
  gestures_.save(paths::gesturesFile());
  memesEdited();
  if (downloaded)
    toast(QString("Added itsgiving's %1 memes to its reactions").arg(done.size()));
  else
    toast(done.isEmpty() ? QString("No itsgiving pictures found there. Pick your itsgiving folder (it has assets inside).")
                         : QString("Copied %1 pictures from your itsgiving clone, matched by name").arg(done.size()));
  return int(done.size());
}

void Controller::probeSound(memes::Meme &m) const {
  if (m.kind == memes::Kind::Video && m.sound.isEmpty())
    m.clipAudio = audio::hasAudio(QDir(paths::mediaDir()).filePath(m.file));
}

void Controller::fetchItsgiving() {
  memes::fetchItsgiving(this, [this](std::shared_ptr<QTemporaryDir> folder, const QString &error) {
    if (!folder) {
      qWarning() << "itsgiving's pictures weren't downloaded (tried again next start):" << error;
      return;
    }
    if (importItsgiving(folder->path(), true) > 0) {
      settings_.itsgivingFetched = true;
      settings_.save(paths::settingsFile());
    }
  });
}

}  // namespace racg::app
