#include "actions/runner.hpp"

#include "common/paths.hpp"
#include "common/settings.hpp"
#include "output/pipeline.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace racg::actions {

namespace {
constexpr size_t kUndoDepth = 20;
}

Runner::Runner(Deps d) : d_(std::move(d)) { addBuiltIns(); }

void Runner::addBuiltIns() {
  // Grid / detection on-off: on everywhere if it was off everywhere, else off; ending puts back
  // exactly what each place had.
  auto overlay = [this](Settings::Places Settings::*field, const QString &what) {
    return [this, field, what](const QString &) {
      Settings::Places &p = d_.settings->*field;
      const Settings::Places before = p;
      const bool on = !(p.preview || p.output);
      p = {on, on};
      if (d_.overlaysChanged) d_.overlaysChanged();
      Done r;
      r.label = what + (on ? " on" : " off");
      r.end = [this, field, before] {
        d_.settings->*field = before;
        if (d_.overlaysChanged) d_.overlaysChanged();
      };
      return r;
    };
  };
  handle("overlay.grid", overlay(&Settings::grid, "grid"));
  handle("overlay.detection", overlay(&Settings::detection, "detection overlay"));

  handle("gestures.pause", [this](const QString &) {
    const bool was = d_.paused();
    d_.setPaused(!was);
    Q_EMIT message(was ? "Gestures resumed" : "Gestures paused: show the same gesture to resume");
    Done r;
    r.label = was ? "resume" : "pause";
    r.revert = [this, was] { d_.setPaused(was); };
    return r;
  });

  handle("gestures.undo", [this](const QString &) {
    undo();
    Done r;
    r.label.clear();   // undo isn't itself undone
    return r;
  });

  handle("snapshot", [this](const QString &) {
    Done r;
    const QImage still = d_.pipeline->clean();
    if (still.isNull()) {
      r.ok = false;
      r.label = "No picture to snapshot yet";
      return r;
    }
    const QString file = QDir(paths::snapshotsDir())
                             .filePath("Snapshot " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss") + ".png");
    if (!still.save(file)) {
      r.ok = false;
      r.label = "The snapshot couldn't be saved in " + QDir::toNativeSeparators(paths::snapshotsDir());
      return r;
    }
    Q_EMIT message("Snapshot saved: " + QFileInfo(file).fileName());
    r.label = "snapshot";
    r.revert = [file] { QFile::remove(file); };
    return r;
  });

  handle("camera.off", [this](const QString &) {
    QImage pic;
    if (!d_.settings->cameraOffPicture.isEmpty()) pic.load(d_.settings->cameraOffPicture);
    d_.pipeline->setCameraOff(true, pic);
    Q_EMIT message("Camera off");
    Done r;
    r.label = "camera off";
    r.end = [this] {
      d_.pipeline->setCameraOff(false, {});
      Q_EMIT message("Camera on");
    };
    return r;
  });
}

void Runner::start(const gestures::Command &c) {
  const Situation s = d_.situation();
  const Info inf = info(c.action, s, *d_.memes);
  if (!available(inf.need, s)) {
    Q_EMIT message(inf.label + ": " + needText(inf.need));
    return;
  }
  const QString base = isMeme(c.action) ? QString("meme") : c.action.section('|', 0, 0);
  const QString arg = isMeme(c.action) ? memeId(c.action) : c.action.section('|', 1);
  auto h = handlers_.find(base);
  if (h == handlers_.end()) {
    Q_EMIT message(inf.label + " isn't available here");
    return;
  }
  Done r = h->second(arg);
  if (!r.ok) {
    if (!r.label.isEmpty()) Q_EMIT message(r.label);
    return;
  }
  if (r.end) {
    auto old = running_.find(c.gesture);
    if (old != running_.end()) old->second();   // a gesture runs one action at a time
    running_[c.gesture] = r.end;
  }
  if (!r.label.isEmpty()) {
    undo_.push_back({r.label, r.end ? c.gesture : QString(), r.revert});
    if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
  }
  Q_EMIT changed();
}

void Runner::end(const gestures::Command &c) {
  auto it = running_.find(c.gesture);
  if (it == running_.end()) return;
  auto fn = std::move(it->second);
  running_.erase(it);
  fn();
  Q_EMIT changed();
}

void Runner::endAll() {
  auto all = std::move(running_);
  running_.clear();
  for (auto &[g, fn] : all) fn();
  Q_EMIT changed();
}

void Runner::run(const QString &action) { start({gestures::Command::Start, "window:" + action, action, "window"}); }

void Runner::undo() {
  while (!undo_.empty()) {
    Entry e = std::move(undo_.back());
    undo_.pop_back();
    if (!e.gesture.isEmpty()) {
      auto it = running_.find(e.gesture);
      if (it == running_.end()) continue;   // already ended by itself: undo the one before
      auto fn = std::move(it->second);
      running_.erase(it);
      fn();
      Q_EMIT forgotten(e.gesture);
    } else if (e.revert) {
      e.revert();
    } else {
      Q_EMIT message("Can't undo " + e.label);
      Q_EMIT changed();
      return;
    }
    Q_EMIT message("Undid " + e.label);
    Q_EMIT changed();
    return;
  }
}

}  // namespace racg::actions
