// The app: owns the settings, both libraries, your calibration, and one channel per picture it
// watches, and moves frames and observations between them. The window only reads from it and calls
// it; nothing in the window talks to a camera, the engine or the link itself.
//
// One app, two ways to run:
//   On its own: one channel, the webcam. The output goes to the virtual cameras; OBS (when open)
//     is driven through its WebSocket.
//   Attached to Rely-A-Cam: the OBS plugin connects over the link (docs/LINK.md) and sends each
//     phone that has gestures on. One channel per phone; the plugin draws the phone's own picture
//     and this app sends only the layer on top (memes, overlays) and the framing crop. Phone and
//     OBS actions go back to the plugin, which does them.
#pragma once

#include "actions/registry.hpp"
#include "actions/runner.hpp"
#include "audio/sound.hpp"
#include "common/settings.hpp"
#include "framing/framer.hpp"
#include "gestures/engine.hpp"
#include "gestures/library.hpp"
#include "gestures/sessions.hpp"
#include "link/server.hpp"
#include "memes/library.hpp"
#include "memes/show.hpp"
#include "obs/websocket.hpp"
#include "output/pipeline.hpp"
#include "vision/camera.hpp"
#include "vision/detector.hpp"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace racg::output {
class ObsCameraSink;
class WinCameraSink;
}

namespace racg::app {

// One picture being watched: the webcam, or one phone through Rely-A-Cam.
struct Channel {
  explicit Channel(gestures::Library &lib) : engine(lib) {}
  QString key;               // "webcam", or the phone's device id
  QString name;              // the camera's or phone's name
  bool phone = false;
  vision::Detector detector;
  gestures::Engine engine;
  output::Pipeline pipeline;
  framing::Framer framer;
  memes::Show show;
  std::unique_ptr<actions::Runner> runner;
  std::shared_ptr<const vision::Observation> obs;
  std::atomic<bool> framePosted{false};
  // Phones only.
  QSize outSize{1280, 720};  // the phone source's size in OBS
  QJsonObject state;         // torch, mic, micOnly, front, zoom... as the plugin last said
  bool layerEmpty = true;
  QRectF lastCrop{0, 0, 1, 1};
  output::DetectionState det;   // what the detection overlay and face map show now
  bool stopped = false;         // the stop sign has fired this hold
};

class Controller : public QObject {
  Q_OBJECT
 public:
  Controller();
  ~Controller() override;
  void start();      // loads everything, opens the camera, starts seeing, listens for Rely-A-Cam
  void shutdown();   // ends running actions, stops cameras and detectors, saves
  void setStill(const QString &file);   // developer aid: a picture as the camera (before start)
  void setAttachLaunch(bool on) { attachLaunch_ = on; }   // started by Rely-A-Cam (--attach)
  std::function<bool()> windowVisible;   // set by main: the window is open

  // ---- state the window reads (the channel shown) ----
  Settings &settings() { return settings_; }
  gestures::Library &gestures() { return gestures_; }
  memes::Library &memes() { return memes_; }
  const gestures::Baseline &baseline() const { return baseline_; }
  const gestures::Engine &engine() const { return cur().engine; }
  const gestures::Recorder &recorder() const { return recorder_; }
  const gestures::Tester &tester() const { return tester_; }
  const gestures::Calibrator &calibrator() const { return calibrator_; }
  actions::Runner &runner() { return *cur().runner; }
  output::Pipeline &pipeline() { return cur().pipeline; }
  std::shared_ptr<const vision::Observation> observation() const { return cur().obs; }
  actions::Situation situation() const;
  QStringList obsScenes() const;
  QStringList obsHotkeys() const;
  bool obsFound() const;
  bool obsNeedsPassword() const;
  bool obsRecording() const;
  QString cameraName() const;        // the webcam, or the phone shown when attached
  QList<vision::CameraInfo> cameras() const { return cameras_; }
  QString cameraError() const { return cameraError_; }
  QString detectorInfo() const;      // "15 fps detection · 640x360 · GPU (DirectML)"
  QString outputsLabel() const;      // the tools bar's outputs chip
  QString soundError() const { return mixer_.error(); }
  double now() const;
  // Attached: the phones, as (device, name), and which one the window shows.
  QList<std::pair<QString, QString>> phones() const;
  QString shownPhone() const { return attached() ? current_ : QString(); }
  void showPhone(const QString &device);
  bool attached() const { return link_.connected(); }

  // ---- what the window asks for ----
  void setCamera(const QString &id);
  void setGesturesOn(bool on);
  void refreshCameras();
  QString newGesture();                       // a fresh "mine" gesture; returns its id
  void deleteGesture(const QString &id);
  void deleteTake(const QString &gestureId, int index);
  void recordTake(const QString &gestureId);  // 3-2-1, then 2 s
  void cancelRecording();
  void testLive(const QString &gestureId);
  void misfireCheck(const QString &gestureId);
  void stopTest();
  void calibrate();
  void undo();
  void gesturesEdited();                      // save + let the engine forget what changed
  void memesEdited();
  void settingsEdited();
  void addMemes(const QStringList &files);
  // Controls (gestures/controls.hpp): a conflicting control is turned off, with a toast.
  void setControlOn(const QString &id, bool on);
  void setControlTrigger(const QString &id, const QString &gestureId);
  void addControl(const QString &id);
  void removeControl(const QString &id);
  const output::DetectionState &detection() const { return cur().det; }
  int importItsgiving(const QString &folder, bool downloaded = false);
  QString takePicture(const QString &file) const;   // full path of a take's picture
  void toast(const QString &text);

 Q_SIGNALS:
  void frame();                 // a new composed frame for the preview
  void observed();              // a new observation (15 fps): overlay, Test meter, sessions
  void libraryChanged();        // gestures or memes changed: rebuild rows
  void statusChanged();         // situation, camera, OBS, recording, undo
  void toastShown(const QString &text);
  void takeRecorded(const QString &gestureId);
  void calibrated();
  void showWindow();            // Rely-A-Cam asked for the editor

 private:
  Channel &cur() { return *channels_.at(current_); }
  const Channel &cur() const { return *channels_.at(current_); }
  Channel &addChannel(const QString &key, const QString &name, bool phone);
  void startDetector(Channel &ch);
  void addHandlers(Channel &ch);
  void openCamera();
  void fetchItsgiving();                 // itsgiving's pictures, once, from its GitHub
  void probeSound(memes::Meme &m) const; // a video's own soundtrack
  static constexpr int kAttachWaitMs = 5000;
  framing::Framer::Moves moves() const;      // which pinch moves Controls has on   // --attach: how long to wait for Rely-A-Cam
  void onObservation(Channel &ch, std::shared_ptr<const vision::Observation> obs);
  void runCommands(Channel &ch, const std::vector<gestures::Command> &cmds);
  void applyEngineConfig();
  void applyOutputs();
  void applySound();
  // The link.
  void onAttached();
  void onDetached();
  void onLinkMessage(link::Msg type, const QJsonObject &j);
  void onLinkFrame(const QString &device, const QImage &picture, QSize out);
  void onUpload(const QString &name, const QByteArray &bytes);
  void sendLayers();
  void sendStatus();
  void sendLibrary();
  void sendAction(const Channel &ch, const QString &id, const QString &arg);
  // OBS, through whichever way it's reached.
  void obsDo(const Channel &ch, const QString &id, const QString &arg);
  bool obsStreaming() const;
  QString obsScene() const;
  std::shared_ptr<const audio::Clip> clipFor(const memes::Meme &m);

  Settings settings_;
  gestures::Library gestures_;
  memes::Library memes_;
  gestures::Baseline baseline_;
  gestures::Recorder recorder_;
  gestures::Tester tester_;
  gestures::Calibrator calibrator_;
  std::map<QString, std::unique_ptr<Channel>> channels_;
  QString current_ = "webcam";
  vision::Camera camera_;
  QList<vision::CameraInfo> cameras_;
  QString cameraError_, device_, detectorError_;
  QImage still_;
  bool started_ = false, attachLaunch_ = false, spanSave_ = false;

  link::Server link_;
  QTimer *layerTimer_ = nullptr;
  QJsonObject obsFromPlugin_;          // recording, streaming, scene, scenes, hotkeys
  obs::WebSocket obsWs_;
  audio::Mixer mixer_;
  std::map<QString, std::shared_ptr<const audio::Clip>> clips_;   // by meme id + file
  std::shared_ptr<output::ObsCameraSink> obsCam_;
  std::shared_ptr<output::WinCameraSink> winCam_;
  int hwZoom_ = 0;                     // the webcam's own zoom preset, when it has one
};

}  // namespace racg::app
