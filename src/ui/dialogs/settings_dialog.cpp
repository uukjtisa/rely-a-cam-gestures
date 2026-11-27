#include "ui/dialogs/settings_dialog.hpp"

#include "app/controller.hpp"
#include "audio/sound.hpp"
#include "common/autostart.hpp"
#include "ui/widgets/kit.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace racg::ui {

namespace {

QLabel *h4(const QString &t, QWidget *p) {
  auto *l = new QLabel(t.toUpper(), p);
  l->setProperty("h4", true);
  return l;
}

QHBoxLayout *rowx() {
  auto *h = new QHBoxLayout;
  h->setSpacing(8);
  return h;
}

// "Outputs" and the checkbox texts have a muted tail: "(Windows 11)".
QWidget *tick(QCheckBox *cb, const QString &tail, QWidget *p) {
  auto *w = new QWidget(p);
  auto *h = new QHBoxLayout(w);
  h->setContentsMargins(0, 0, 0, 0);
  h->setSpacing(4);
  h->addWidget(cb);
  if (!tail.isEmpty()) h->addWidget(note(tail, w));
  h->addStretch(1);
  return w;
}

}  // namespace

SettingsDialog::SettingsDialog(app::Controller &c, QWidget *parent) : QDialog(parent), c_(c) {
  setWindowTitle("Settings");
  setFixedWidth(500);
  Settings &s = c_.settings();
  auto *v = new QVBoxLayout(this);
  v->setContentsMargins(16, 14, 16, 14);
  v->setSpacing(9);
  auto *title = new QLabel("Settings", this);
  QFont tf = title->font();
  tf.setPixelSize(14);
  tf.setBold(true);
  title->setFont(tf);
  v->addWidget(title);

  // ---- Outputs ----
  v->addWidget(h4("Outputs (on its own)", this));
  auto *vcam = new QCheckBox("Own virtual camera, \"Rely-A-Cam-Gestures Camera\"", this);
  vcam->setChecked(s.outVirtualCamera);
  v->addWidget(tick(vcam, "(Windows 11)", this));
  auto *obsCam = new QCheckBox("OBS Virtual Camera", this);
  obsCam->setChecked(s.outObsCamera);
  v->addWidget(tick(obsCam, "(OBS installed; it doesn't need to be running)", this));
  v->addWidget(note("Both were ticked in the installer; untick one here any time.", this));
  connect(vcam, &QCheckBox::toggled, this, [this](bool on) {
    c_.settings().outVirtualCamera = on;
    c_.settingsEdited();
  });
  connect(obsCam, &QCheckBox::toggled, this, [this](bool on) {
    c_.settings().outObsCamera = on;
    c_.settingsEdited();
  });
  {
    auto *h = rowx();
    h->addWidget(note("Camera off shows", this));
    auto *off = new QComboBox(this);
    auto fill = [off, this] {
      off->clear();
      off->addItem("A dark frame");
      const QString pic = c_.settings().cameraOffPicture;
      if (!pic.isEmpty()) off->addItem(QFileInfo(pic).fileName());
      off->addItem("Choose a picture...");
      off->setCurrentIndex(pic.isEmpty() ? 0 : 1);
    };
    fill();
    connect(off, &QComboBox::activated, this, [this, off, fill](int i) {
      if (i == off->count() - 1) {
        const QString f = QFileDialog::getOpenFileName(this, "Picture for camera off", {}, "Pictures (*.png *.jpg *.jpeg *.bmp *.webp)");
        if (!f.isEmpty()) c_.settings().cameraOffPicture = f;
      } else if (i == 0) {
        c_.settings().cameraOffPicture.clear();
      }
      c_.settingsEdited();
      fill();
    });
    h->addWidget(off, 1);
    v->addLayout(h);
  }

  // ---- Meme sound ----
  v->addWidget(h4("Meme sound (on its own)", this));
  {
    auto *h = rowx();
    auto *spk = new QRadioButton("To my speakers", this);
    auto *calls = new QRadioButton("Into calls, mixed with my mic (VB-Cable)", this);
    auto *grp = new QButtonGroup(this);
    grp->addButton(spk, int(Settings::MemeSound::Speakers));
    grp->addButton(calls, int(Settings::MemeSound::Calls));
    (s.memeSound == Settings::MemeSound::Calls ? calls : spk)->setChecked(true);
    h->addWidget(spk);
    h->addWidget(calls);
    h->addStretch(1);
    v->addLayout(h);

    // Calls: which real mic goes in with the memes, and whether VB-Cable is there to carry them.
    auto *callsRow = new QWidget(this);
    auto *ch = new QHBoxLayout(callsRow);
    ch->setContentsMargins(22, 0, 0, 0);
    ch->setSpacing(8);
    ch->addWidget(note("My mic", callsRow));
    auto *mic = new QComboBox(callsRow);
    mic->addItem("Windows' default for calls", QString());
    for (const QString &m : audio::microphones()) {
      const QString id = m.section('|', 0, 0), name = m.section('|', 1);
      mic->addItem(name, id);
    }
    mic->setCurrentIndex(std::max(0, mic->findData(s.callMic)));
    connect(mic, &QComboBox::activated, this, [this, mic] {
      c_.settings().callMic = mic->currentData().toString();
      c_.settingsEdited();
    });
    ch->addWidget(mic, 1);
    v->addWidget(callsRow);
    const bool cable = audio::cableInstalled();
    auto *callsNote = note(cable ? "In the call app, pick \"CABLE Output\" as the microphone."
                                 : "VB-Cable isn't installed: get it free from vb-audio.com, then pick \"CABLE Output\" in the call app.",
                           this);
    callsNote->setWordWrap(true);
    v->addWidget(callsNote);
    auto showCalls = [callsRow, callsNote](bool on) {
      callsRow->setVisible(on);
      callsNote->setVisible(on);
    };
    showCalls(s.memeSound == Settings::MemeSound::Calls);
    connect(grp, &QButtonGroup::idClicked, this, [this, showCalls](int id) {
      c_.settings().memeSound = Settings::MemeSound(id);
      c_.settingsEdited();
      showCalls(id == int(Settings::MemeSound::Calls));
      adjustSize();
    });
    if (const QString err = c_.soundError(); !err.isEmpty()) {
      auto *e = note("Sound: " + err, this);
      e->setWordWrap(true);
      v->addWidget(e);
    }
  }

  // ---- Face ---- (the confirm and stop signs are on the Controls tab)
  v->addWidget(h4("Face", this));
  {
    auto *h = rowx();
    auto *cal = new QPushButton("Calibrate neutral face (7 s)...", this);
    connect(cal, &QPushButton::clicked, this, [this] {
      Q_EMIT calibrate();
      accept();
    });
    h->addWidget(cal);
    const auto &b = c_.baseline();
    QString when = "not done yet: it uses an average face until you do";
    if (!b.made.isEmpty()) {
      const QDateTime t = QDateTime::fromString(b.made, Qt::ISODate);
      const QString day = !t.isValid() ? b.made
                          : t.date() == QDate::currentDate() ? QString("today")
                                                             : QLocale().toString(t.date(), QLocale::ShortFormat);
      when = QString("last done %1, %2 frames").arg(day).arg(b.samples);
    }
    h->addWidget(note(when, this), 1);
    v->addLayout(h);
  }

  // ---- OBS ----
  v->addWidget(h4("OBS", this));
  {
    auto *h = rowx();
    if (c_.situation() == actions::Situation::Rely) {
      h->addWidget(new Chip("Attached through Rely-A-Cam: no WebSocket needed", Chip::Ok, this));
    } else {
      const bool found = c_.obsFound(), locked = c_.obsNeedsPassword();
      h->addWidget(new Chip(found    ? QString("OBS WebSocket found on localhost:%1").arg(s.obsPort)
                            : locked ? QString("OBS wants its WebSocket password")
                                     : QString("OBS not found"),
                            found ? Chip::Ok : locked ? Chip::Rec : Chip::Plain, this));
      if (!found && !locked) h->addWidget(note("Turn on Tools > WebSocket Server Settings in OBS", this), 1);
      if (locked) h->addStretch(1);
      auto *pw = new QLineEdit(s.obsPassword, this);
      pw->setEchoMode(QLineEdit::Password);
      pw->setPlaceholderText("password");
      pw->setFixedWidth(110);
      pw->setToolTip("OBS > Tools > WebSocket Server Settings > Show Connect Info");
      connect(pw, &QLineEdit::editingFinished, this, [this, pw] {
        if (pw->text() == c_.settings().obsPassword) return;
        c_.settings().obsPassword = pw->text();
        c_.settingsEdited();
      });
      h->addWidget(pw);
    }
    if (c_.situation() == actions::Situation::Rely) h->addStretch(1);
    v->addLayout(h);
  }
  auto *boot = new QCheckBox("Start with Windows (in the tray)", this);
  boot->setChecked(autostart::enabled());
  connect(boot, &QCheckBox::toggled, this, [this](bool on) {
    autostart::set(on);
    c_.settings().startWithWindows = on;
    c_.settingsEdited();
  });
  v->addWidget(boot);

  auto *end = rowx();
  end->addStretch(1);
  auto *done = new QPushButton("Done", this);
  done->setProperty("on", true);
  done->setDefault(true);
  connect(done, &QPushButton::clicked, this, &QDialog::accept);
  end->addWidget(done);
  v->addLayout(end);
}

}  // namespace racg::ui
