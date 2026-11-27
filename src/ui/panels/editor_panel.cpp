#include "ui/panels/editor_panel.hpp"

#include "app/controller.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/kit.hpp"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QGridLayout>
#include <QStyle>
#include <QtMath>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QActionGroup>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace racg::ui {

using namespace theme;
using gestures::Ends;
using gestures::Kind;

namespace {

// A take's picture, 44 x 34; right-click deletes it.
class TakeThumb : public QWidget {
 public:
  TakeThumb(const QString &file, int n, QWidget *parent) : QWidget(parent), n_(n) {
    setFixedSize(44, 34);
    if (!file.isEmpty()) img_.load(file);
    setToolTip(QString("Take %1 (right-click to delete)").arg(n));
  }
  std::function<void()> onDelete;

 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    p.setClipPath(clip);
    p.fillRect(rect(), grey5);
    if (!img_.isNull()) {
      const QSizeF s = QSizeF(img_.size()).scaled(size(), Qt::KeepAspectRatioByExpanding);
      p.drawImage(QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height()), img_);
    } else {
      QFont f = font();
      f.setPixelSize(11);
      p.setFont(f);
      p.setPen(textLight);
      p.drawText(rect(), Qt::AlignCenter, QString("take %1").arg(n_));
    }
    p.setClipping(false);
    p.setPen(QPen(grey3, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
  }
  void contextMenuEvent(QContextMenuEvent *e) override {
    QMenu m(this);
    m.addAction(QString("Delete take %1").arg(n_), [this] {
      if (onDelete) onDelete();
    });
    m.exec(e->globalPos());
  }

 private:
  QImage img_;
  int n_;
};

QLabel *fieldLabel(const QString &t, int width, QWidget *parent) {
  auto *l = new QLabel(t, parent);
  l->setProperty("muted", true);
  l->setFixedWidth(width);
  return l;
}

QHBoxLayout *line() {
  auto *h = new QHBoxLayout;
  h->setSpacing(6);
  h->setContentsMargins(0, 0, 0, 0);
  return h;
}

QString endsLabel(const gestures::GestureDef &g) {
  switch (g.ends) {
    case Ends::Held: return "While held (lingers ⅓ s)";
    case Ends::Again: return "Same gesture again";
    case Ends::After: return QString("After %1 s").arg(g.secs);
  }
  return {};
}

QString holdText(float s) { return QString("hold %1 s").arg(double(s), 0, 'g', 2); }

}  // namespace

EditorPanel::EditorPanel(app::Controller &c, QWidget *parent) : QWidget(parent), c_(c) {
  lay_ = new QVBoxLayout(this);
  lay_->setContentsMargins(8, 8, 8, 8);
  lay_->setSpacing(9);
  connect(&c_, &app::Controller::observed, this, &EditorPanel::refreshTest);
  connect(&c_, &app::Controller::takeRecorded, this, [this](const QString &id) {
    if (id == id_) QTimer::singleShot(0, this, &EditorPanel::build);
  });
  connect(&c_, &app::Controller::statusChanged, this, [this] {
    // Only what the editor shows: recording started or stopped, or the situation changed (the
    // action's "needs"). Anything else would steal focus from the name field mid-edit.
    const bool rec = c_.recorder().phase() != gestures::Recorder::Phase::Idle;
    const int sit = int(c_.situation());
    if (rec == lastRecording_ && sit == lastSituation_) return;
    lastRecording_ = rec;
    lastSituation_ = sit;
    if (!id_.isEmpty() && isVisible()) QTimer::singleShot(0, this, &EditorPanel::build);
  });
}

gestures::GestureDef *EditorPanel::g() { return c_.gestures().find(id_); }

void EditorPanel::open(const QString &gestureId) {
  if (gestureId != id_) c_.stopTest();
  id_ = gestureId;
  build();
}

void EditorPanel::build() {
  setUpdatesEnabled(false);
  while (QLayoutItem *it = lay_->takeAt(0)) {
    if (it->widget()) it->widget()->deleteLater();
    if (it->layout()) {
      while (QLayoutItem *in = it->layout()->takeAt(0)) {
        if (in->widget()) in->widget()->deleteLater();
        delete in;
      }
    }
    delete it;
  }
  meter_ = nullptr;
  matchVal_ = result_ = nullptr;
  testBtn_ = misBtn_ = nullptr;
  gestures::GestureDef *gd = g();
  if (!gd) {
    setUpdatesEnabled(true);
    return;
  }
  const bool mine = gd->kind == Kind::Mine;

  // ← Library ............ (On)
  {
    auto *h = line();
    auto *backBtn = new QPushButton("← Library", this);
    connect(backBtn, &QPushButton::clicked, this, [this] {
      c_.stopTest();
      c_.cancelRecording();
      Q_EMIT back();
    });
    h->addWidget(backBtn);
    h->addStretch(1);
    auto *sw = new Switch(gd->on ? "On" : "Off", this);
    sw->setChecked(gd->on);
    connect(sw, &Switch::toggled, this, [this, sw](bool on) {
      if (auto *x = g()) x->on = on;
      sw->setText(on ? "On" : "Off");
      sw->updateGeometry();
      c_.gesturesEdited();
    });
    h->addWidget(sw);
    lay_->addLayout(h);
  }
  // icon + name
  {
    auto *h = line();
    auto *ic = new QLabel(gd->icon, this);
    ic->setStyleSheet("font-family: 'Segoe UI Emoji'; font-size: 20px;");
    h->addWidget(ic);
    auto *name = new QLineEdit(gd->name, this);
    name->setEnabled(mine);
    name->setMaxLength(40);
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
      auto *x = g();
      const QString t = name->text().trimmed();
      if (!x || t.isEmpty() || t == x->name) return;
      x->name = t;
      c_.gesturesEdited();
    });
    h->addWidget(name, 1);
    lay_->addLayout(h);
  }
  lay_->addWidget(note(gd->how(), this));

  // RECORDING
  lay_->addWidget(sectionTitle("Recording", this));
  if (mine) {
    auto *takes = line();
    takes->setSpacing(5);
    if (gd->takes.empty()) takes->addWidget(note("no takes yet", this));
    for (size_t i = 0; i < gd->takes.size(); ++i) {
      const QString f = gd->takes[i].thumb.isEmpty() ? QString() : c_.takePicture(gd->takes[i].thumb);
      auto *t = new TakeThumb(f, int(i) + 1, this);
      t->onDelete = [this, i] {
        c_.deleteTake(id_, int(i));
        QTimer::singleShot(0, this, &EditorPanel::build);
      };
      takes->addWidget(t);
    }
    takes->addStretch(1);
    lay_->addLayout(takes);
    auto *h = line();
    const bool recording = c_.recorder().phase() != gestures::Recorder::Phase::Idle;
    auto *rec = new QPushButton(recording ? "Cancel recording" : gd->takes.empty() ? "Record take" : "Record another take", this);
    rec->setProperty("on", true);
    connect(rec, &QPushButton::clicked, this, [this, recording] {
      if (recording) c_.cancelRecording();
      else c_.recordTake(id_);
    });
    h->addWidget(rec);
    h->addWidget(note("3-2-1, then hold 2 s", this));
    h->addStretch(1);
    lay_->addLayout(h);
  } else {
    lay_->addWidget(note("Ready-made: no recording needed. Sensitivity below tunes it to you.", this));
  }
  // Counts
  {
    auto *h = line();
    h->addWidget(note("Counts", this));
    const struct {
      const char *label;
      bool gestures::Parts::*field;
    } parts[] = {{"Left hand", &gestures::Parts::leftHand},
                 {"Right hand", &gestures::Parts::rightHand},
                 {"Face", &gestures::Parts::face},
                 {"Upper body", &gestures::Parts::body}};
    auto *box = new QWidget(this);
    auto *flow = new QGridLayout(box);
    flow->setContentsMargins(0, 0, 0, 0);
    flow->setHorizontalSpacing(10);
    flow->setVerticalSpacing(2);
    int i = 0;
    for (const auto &p : parts) {
      auto *cb = new QCheckBox(p.label, box);
      cb->setChecked(gd->parts.*(p.field));
      cb->setEnabled(mine);
      if (!mine) cb->setToolTip("What its rule looks at; ready-made gestures can't change it");
      auto field = p.field;
      connect(cb, &QCheckBox::toggled, this, [this, field](bool on) {
        if (auto *x = g()) x->parts.*field = on;
        c_.gesturesEdited();
        QTimer::singleShot(0, this, &EditorPanel::build);   // confirm depends on the hands
      });
      flow->addWidget(cb, i / 2, i % 2);
      ++i;
    }
    h->addWidget(box, 1);
    lay_->addLayout(h);
  }

  // TEST
  lay_->addWidget(sectionTitle("Test", this));
  {
    auto *h = line();
    h->addWidget(fieldLabel("Match", 44, this));
    meter_ = new Meter(this);
    h->addWidget(meter_, 1);
    matchVal_ = new QLabel("-", this);
    matchVal_->setFixedWidth(40);
    matchVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont bf = matchVal_->font();
    bf.setBold(true);
    matchVal_->setFont(bf);
    h->addWidget(matchVal_);
    lay_->addLayout(h);
  }
  {
    auto *h = line();
    testBtn_ = new QPushButton("Test live", this);
    connect(testBtn_, &QPushButton::clicked, this, [this] {
      if (c_.tester().mode() == gestures::Tester::Mode::Live && c_.tester().gestureId() == id_) c_.stopTest();
      else c_.testLive(id_);
      refreshTest();
    });
    misBtn_ = new QPushButton("Misfire check (10 s)", this);
    connect(misBtn_, &QPushButton::clicked, this, [this] {
      c_.misfireCheck(id_);
      refreshTest();
    });
    h->addWidget(testBtn_);
    h->addWidget(misBtn_);
    h->addStretch(1);
    lay_->addLayout(h);
  }
  result_ = new QLabel(this);
  result_->setProperty("result", true);
  result_->setWordWrap(true);
  lay_->addWidget(result_);

  // DOES
  lay_->addWidget(sectionTitle("Does", this));
  {
    auto *h = line();
    h->addWidget(fieldLabel("Action", 44, this));
    auto *pick = new Picker(this);
    const auto s = c_.situation();
    const auto inf = actions::info(gd->action, s, c_.memes());
    const bool ok = actions::available(inf.need, s);
    pick->setText(inf.label + (ok ? QString() : " (" + actions::needText(inf.need) + ")"), !ok);
    connect(pick, &Picker::clicked, this, &EditorPanel::actionMenu);
    h->addWidget(pick, 1);
    lay_->addLayout(h);
  }
  {
    auto *h = line();
    h->addWidget(fieldLabel("Ends", 44, this));
    auto *pick = new Picker(this);
    pick->setText(endsLabel(*gd));
    connect(pick, &Picker::clicked, this, &EditorPanel::endsMenu);
    h->addWidget(pick, 1);
    lay_->addLayout(h);
  }
  {
    auto *cb = new QCheckBox("The stop sign ends it", this);
    cb->setChecked(gd->stop);
    connect(cb, &QCheckBox::toggled, this, [this](bool on) {
      if (auto *x = g()) x->stop = on;
      c_.gesturesEdited();
    });
    lay_->addWidget(cb);
  }
  {
    const bool both = gd->usesBothHands();
    auto *cb = new QCheckBox(QString("Needs confirm (other hand shows %1)").arg(vision::signLabel(c_.settings().confirmSign)), this);
    cb->setChecked(gd->confirm && !both);
    cb->setEnabled(!both);
    if (both) cb->setToolTip("It uses both hands, so there's no other hand to confirm with");
    connect(cb, &QCheckBox::toggled, this, [this](bool on) {
      if (auto *x = g()) x->confirm = on;
      c_.gesturesEdited();
    });
    lay_->addWidget(cb);
  }
  {
    auto *h = line();
    h->addWidget(fieldLabel("Sensitivity", 70, this));
    auto *sl = new QSlider(Qt::Horizontal, this);
    sl->setRange(gestures::kSensMin, gestures::kSensMax);
    sl->setValue(gd->sens);
    sl->setToolTip(QString("Fires when the match passes %1 %").arg(gd->sens));
    connect(sl, &QSlider::valueChanged, this, [this, sl](int v) {
      if (auto *x = g()) x->sens = v;
      sl->setToolTip(QString("Fires when the match passes %1 %").arg(v));
      refreshTest();
    });
    connect(sl, &QSlider::sliderReleased, this, [this] { c_.gesturesEdited(); });
    h->addWidget(sl, 1);
    auto *hold = new QPushButton(holdText(gd->hold) + " ▾", this);
    hold->setFlat(true);
    hold->setToolTip("How long it must be held before it fires");
    hold->setStyleSheet(QString("QPushButton { background: transparent; border: 0; color: %1; padding: 2px 4px; }"
                                "QPushButton:hover { color: %2; }")
                            .arg(hex(muted), hex(textLight)));
    connect(hold, &QPushButton::clicked, this, [this, hold] { holdMenu(hold->mapToGlobal(QPoint(0, hold->height()))); });
    h->addWidget(hold);
    lay_->addLayout(h);
  }
  if (mine) {
    auto *h = line();
    h->addStretch(1);
    auto *del = new QPushButton("Delete gesture", this);
    del->setProperty("warn", true);
    connect(del, &QPushButton::clicked, this, [this] {
      const QString id = id_;
      id_.clear();
      c_.stopTest();
      c_.cancelRecording();
      c_.deleteGesture(id);
      Q_EMIT back();
    });
    h->addWidget(del);
    lay_->addLayout(h);
  }
  lay_->addStretch(1);
  refreshTest();
  setUpdatesEnabled(true);
}

void EditorPanel::refreshTest() {
  if (!meter_ || !result_) return;
  const gestures::GestureDef *gd = g();
  if (!gd) return;
  const auto &t = c_.tester();
  const bool mineTest = t.gestureId() == id_;
  const auto mode = mineTest ? t.mode() : gestures::Tester::Mode::Off;
  if (mineTest && mode == gestures::Tester::Mode::Off && !t.result.isEmpty()) lastResult_[id_] = t.result;

  testBtn_->setText(mode == gestures::Tester::Mode::Live ? "Stop test" : "Test live");
  testBtn_->setChecked(false);
  testBtn_->setProperty("on", mode == gestures::Tester::Mode::Live);
  testBtn_->style()->unpolish(testBtn_);
  testBtn_->style()->polish(testBtn_);
  misBtn_->setEnabled(mode != gestures::Tester::Mode::Misfire);

  if (mode == gestures::Tester::Mode::Off) {
    meter_->set(0, float(gd->sens));
    matchVal_->setText("-");
    auto r = lastResult_.find(id_);
    result_->setText(r != lastResult_.end() ? r->second
                                            : QString("Hold the gesture while Test live runs: the bar must pass the white line to fire."));
    return;
  }
  const float pct = t.pct();
  meter_->set(pct, float(gd->sens));
  matchVal_->setText(QString("%1%").arg(qRound(std::min(pct, 999.f))));
  if (mode == gestures::Tester::Mode::Live)
    result_->setText(pct >= gd->sens ? QString("Would fire now (%1% over the %2% line).").arg(qRound(pct)).arg(gd->sens)
                                     : QString("Below the line: wouldn't fire."));
  else
    result_->setText(QString("Move normally: talk, scratch, look around... %1 s").arg(qCeil(t.secondsLeft(c_.now()))));
}

// Menus stay open on a pick (kit.hpp), so the panel is rebuilt once the menu has closed.
void EditorPanel::execMenu(QMenu &m, QPoint at) {
  // One exclusive group per menu: these are pick-one lists, submenus included.
  auto *one = new QActionGroup(&m);
  std::function<void(QMenu *)> group = [&](QMenu *menu) {
    for (QAction *a : menu->actions()) {
      if (a->menu()) group(a->menu());
      else if (a->isCheckable()) one->addAction(a);
    }
  };
  group(&m);
  dirty_ = false;
  m.exec(at);
  if (dirty_) QTimer::singleShot(0, this, &EditorPanel::build);
}

void EditorPanel::actionMenu(QPoint at) {
  QMenu m(this);
  fillActionMenu(m);
  execMenu(m, at);
}

void EditorPanel::fillActionMenu(QMenu &m) {
  gestures::GestureDef *gd = g();
  if (!gd) return;
  const QString id = id_;
  auto pick = [this, id](const QString &action) {
    gestures::GestureDef *x = c_.gestures().find(id);
    if (!x) return;
    const bool changed = x->action != action;
    x->action = action;
    if (changed && !action.isEmpty()) {
      if (actions::isMeme(action)) x->ends = Ends::Held;
      else if (x->ends == Ends::Held) x->ends = Ends::Again;
      if (actions::defaultConfirm(action) && !x->usesBothHands()) x->confirm = true;
    }
    c_.gesturesEdited();
    dirty_ = true;
  };
  if (const auto *k = c_.gestures().controls.byTrigger(id)) {
    addMenuNote(&m, "This gesture runs " + gestures::controlLabel(k->id) + ". Change it on the Controls tab.");
    return;
  }
  QAction *none = m.addAction("None", [pick] { pick({}); });
  none->setCheckable(true);
  none->setChecked(gd->action.isEmpty());
  const auto s = c_.situation();
  QStringList hidden;
  for (const auto &grp : actions::groups(s, c_.memes(), c_.obsScenes(), c_.obsHotkeys())) {
    if (grp.name != "Meme") continue;   // every other job is a control (the Controls tab)
    if (!actions::available(grp.need, s)) {
      hidden << grp.name + " (" + actions::needText(grp.need) + ")";
      continue;
    }
    addMenuHeader(&m, grp.name);
    if (grp.items.empty()) addMenuNote(&m, grp.name == "Meme" ? "No memes yet: add some on the Memes tab." : "Nothing here yet.");
    for (const auto &it : grp.items) {
      if (!it.subs.isEmpty()) {
        QMenu *sub = m.addMenu(it.label);
        for (const auto &sname : it.subs) {
          const QString full = it.id + "|" + sname;
          QAction *a = sub->addAction(sname, [pick, full] { pick(full); });
          a->setCheckable(true);
          a->setChecked(gd->action == full);
        }
        continue;
      }
      if (it.id == "obs.scene" || it.id == "obs.hotkey") {
        QMenu *sub = m.addMenu(it.label);
        addMenuNote(sub, "OBS hasn't listed any yet.");
        continue;
      }
      QAction *a = m.addAction(it.label, [pick, aid = it.id] { pick(aid); });
      a->setCheckable(true);
      a->setChecked(gd->action == it.id);
    }
  }
  if (!hidden.isEmpty()) {
    m.addSeparator();
    addMenuNote(&m, "Not offered here: " + hidden.join(", ") + ". Gestures already set to these keep them, greyed out.");
  }
}

void EditorPanel::endsMenu(QPoint at) {
  gestures::GestureDef *gd = g();
  if (!gd) return;
  QMenu m(this);
  const QString id = id_;
  auto set = [this, id](Ends e, int secs) {
    if (auto *x = c_.gestures().find(id)) {
      x->ends = e;
      if (secs) x->secs = secs;
    }
    c_.gesturesEdited();
    dirty_ = true;
  };
  QAction *a = m.addAction("While held (lingers ⅓ s) - itsgiving's way", [set] { set(Ends::Held, 0); });
  a->setCheckable(true);
  a->setChecked(gd->ends == Ends::Held);
  a = m.addAction("Same gesture again", [set] { set(Ends::Again, 0); });
  a->setCheckable(true);
  a->setChecked(gd->ends == Ends::Again);
  QMenu *after = m.addMenu("After N seconds");
  for (int n : {2, 3, 5, 10}) {
    QAction *s = after->addAction(QString("%1 s").arg(n), [set, n] { set(Ends::After, n); });
    s->setCheckable(true);
    s->setChecked(gd->ends == Ends::After && gd->secs == n);
  }
  execMenu(m, at);
}

void EditorPanel::holdMenu(QPoint at) {
  gestures::GestureDef *gd = g();
  if (!gd) return;
  QMenu m(this);
  const QString id = id_;
  for (float s : gestures::kHoldChoices) {
    QAction *a = m.addAction(holdText(s), [this, id, s] {
      if (auto *x = c_.gestures().find(id)) x->hold = s;
      c_.gesturesEdited();
      dirty_ = true;
    });
    a->setCheckable(true);
    a->setChecked(std::abs(gd->hold - s) < 1e-3f);
  }
  execMenu(m, at);
}

}  // namespace racg::ui
